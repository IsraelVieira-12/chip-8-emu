#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

//#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

// SDL Container object
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;
} sdl_t;

// Emulator configuration object
typedef struct {
    uint32_t window_width; // SDL window width
    uint32_t window_height; // SDL window height
    uint32_t fg_color;  // Foreground Color RGBA8888
    uint32_t bg_color;  // Background Color RGBA8888
    uint32_t scale_factor; // Amount to scale a CHIP8 pixel by eg 20x will be 20x larger window
    bool pixel_outlines; // Draw pixel outlines yes/no
    uint32_t insts_per_second; // CHIP8 CPU "clock rate" or hz
    uint32_t square_wave_freq; // Frequency of square wave sound in hz
    uint16_t volume; // How loud or not is the sound
    uint32_t audio_sample_rate;
} config_t;

// Emulator states
typedef enum {
    QUIT,
    RUNNING,
    PAUSED
} emulator_state_t;

// CHIP8 Instructions format
typedef struct {
    uint16_t opcode;
    uint16_t NNN; // 12 bit address/constant
    uint8_t NN; // 8 bit constant
    uint8_t N; // 4 bit constant
    uint8_t X; // 4 bit register identifier
    uint8_t Y; // 4 bit register identifier
} instruction_t;

// CHIP8 Machine Object
typedef struct {
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64*32]; // Emulate original CHIP8 pixels
    uint16_t stack[12]; // Subroutine stack
    uint16_t *stack_ptr;
    uint8_t V[16]; // Data registers V0-VF
    uint16_t I; // Index registers
    uint16_t PC; // Program counter
    uint8_t delay_timer; // Decrement at 60hz when >0
    uint8_t sound_timer; // Decrement at 60hz and plays tone when >0
    bool keypad[16]; // Hexadecimal keypad 0x0-0xF
    const char *rom_name; // Currently running ROM
    instruction_t inst;  // Currently executing instruction
} chip8_t;

// SDL Audio Callback
// Fill out stream/audio buffer with audio data
void audio_callback(void *userdata, uint8_t *stream, int len){
    config_t *config = (config_t *) userdata;

    int16_t *audio_data = (int16_t *) stream;
    static uint32_t running_sample_index = 0;
    const int32_t square_wave_period = config->audio_sample_rate / config->square_wave_freq;
    const int32_t half_square_wave_period = square_wave_period / 2;

    // We are filling out 2 bytes at a time (int16_t), len is in bytes
    for(int i = 0; i < len / 2; i++){

        audio_data[i] = ((running_sample_index++ / half_square_wave_period) % 2) ? config->volume : -config->volume;
    }

}

// Initialize SDL
bool init_sdl(sdl_t *sdl, config_t *config, const char rom_name[]){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER ) != 0) {
        SDL_Log("Could not initialize SDL subsystem! %s\n", SDL_GetError());
        return false;
    }

    char app_name[128] = "CHIP8 Emulator - ";
    strcat(app_name, rom_name);

    sdl->window = SDL_CreateWindow(app_name, SDL_WINDOWPOS_CENTERED, 
                                SDL_WINDOWPOS_CENTERED,
                                config->window_width * config->scale_factor,
                                config->window_height * config->scale_factor, 
                                0);
    
    if(!sdl->window){
        SDL_Log("Could not create window! %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if(!sdl->renderer){
        SDL_Log("Could not create SDL renderer! %s\n", SDL_GetError());
        return false;
    }

    // Initialize SDL Audio
    sdl->want = (SDL_AudioSpec){
        .freq = 44100, // 44100hz "CD" quality
        .format = AUDIO_S16LSB, // 16-bit signed little-endian  
        .channels = 1, // Mono
        .samples = 4096,
        .callback = audio_callback,
        .userdata = config, // Userdata passed to audio callback
    };

    sdl->dev = SDL_OpenAudioDevice(NULL, 0, &sdl->want, &sdl->have, 0);

    if(sdl->dev == 0){
        SDL_Log("Could not get an audio device %s\n", SDL_GetError());
        return false;
    }

    if((sdl->want.format != sdl->have.format) || (sdl->want.channels != sdl->have.channels)){
        SDL_Log("Could not get the desired audio spec\n");
        return false;
    }

    return true;
}

// Setup initial emulator configuration from arguments
bool set_config_from_args(config_t *config, const int argc, char **argv){

    // Set defaults
    *config = (config_t){
        .window_width = 64,
        .window_height = 32,
        .fg_color = 0xFFFFFFFF, // WHITE
        .bg_color = 0x000000FF, // BLACK
        .scale_factor = 20, // Default resolution will be 1280x640
        .pixel_outlines = true, // Draw pixel "outlines" ny default
        .insts_per_second = 700, // Number of instruction to emulate per second
        .square_wave_freq = 440, // Frequency of square wave sound
        .volume = 3000, // Volume of sound
        .audio_sample_rate = 44100, // CD quality audio
    };

    // Override defaults from passed arguments
    for(int i = 1; i < argc; i++){
        (void) argv[i];
    }

    return true; // Success
}

//Initialize CHIP8 Machine
bool init_chip8(chip8_t *chip8, const char rom_name[]){
    const uint32_t entry_point = 0x200; //CHIP8 Roms will be loaded to 0x200
    const uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0,   // 0   
        0x20, 0x60, 0x20, 0x20, 0x70,   // 1  
        0xF0, 0x10, 0xF0, 0x80, 0xF0,   // 2 
        0xF0, 0x10, 0xF0, 0x10, 0xF0,   // 3
        0x90, 0x90, 0xF0, 0x10, 0x10,   // 4    
        0xF0, 0x80, 0xF0, 0x10, 0xF0,   // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0,   // 6
        0xF0, 0x10, 0x20, 0x40, 0x40,   // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0,   // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0,   // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90,   // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0,   // B
        0xF0, 0x80, 0x80, 0x80, 0xF0,   // C
        0xE0, 0x90, 0x90, 0x90, 0xE0,   // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0,   // E
        0xF0, 0x80, 0xF0, 0x80, 0x80,   // F
    };
    
    // Load font
    memcpy(&chip8->ram[0], font, sizeof(font));

    // Open ROM file
    FILE *rom = fopen(rom_name, "rb");
    if(!rom){
        SDL_Log("Rom file is %s is invalid or does not exist\n", rom_name);
        return false;
    }

    // Get and check rom size
    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof chip8->ram - entry_point;
    rewind(rom);

    if(rom_size > max_size){

#ifdef _WIN32
        SDL_Log("Rom file %s is too big!", rom_name);
#else
        SDL_Log("Rom file %s is too big! Rom size: %zu, Max size allowed: %zu\n", rom_name, rom_size, max_size);
#endif

        return false;
    }

    // Load ROM
    if (fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1){
        SDL_Log("Could not read ROM file %s into CHIP8 memory\n", rom_name);
        return false;
    }

    fclose(rom);

    // Set chip8 machine defaults
    chip8->state = RUNNING; // Default machine state to on/running
    chip8->PC = entry_point; // Start program counter at ROM entry point
    chip8->rom_name = rom_name; // Set ROM name
    chip8->stack_ptr = &chip8->stack[0];

    return true;
}

void final_cleanup(const sdl_t sdl){
    SDL_DestroyRenderer(sdl.renderer); // Destroy renderer
    SDL_DestroyWindow(sdl.window); // Destroy window
    SDL_CloseAudioDevice(sdl.dev); // Close audio device
    SDL_Quit(); // Shut down SDL subsystem
}

// Clear screen / SDl Window to background color
void clear_screen(const sdl_t sdl, const config_t config){
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >> 8) & 0xFF;
    const uint8_t a = (config.bg_color >> 0) & 0xFF;

    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

// Update window with any changes
void update_screen(const sdl_t sdl, const config_t config, const chip8_t chip8){
    SDL_Rect rect = {.x = 0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};
    // Grab color values ot draw

    const uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
    const uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
    const uint8_t fg_b = (config.fg_color >> 8) & 0xFF;
    const uint8_t fg_a = (config.fg_color >> 0) & 0xFF;

    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    const uint8_t bg_b = (config.bg_color >> 8) & 0xFF;
    const uint8_t bg_a = (config.bg_color >> 0) & 0xFF;

    for(uint32_t i = 0; i < sizeof chip8.display; i++){
        // Translate 1D index i value to 2D X/Y Coordinates
        rect.x = (i % config.window_width) * config.scale_factor;
        rect.y = (i / config.window_width) * config.scale_factor;

        if(chip8.display[i]){
            SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);

            // If user requested drawing pixel outlines, draw those here
            if(config.pixel_outlines){
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }

        }
        else{
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }
    SDL_RenderPresent(sdl.renderer);
}

// Handle Input
// CHIP8 Keypad     QWERTY
// 123C             1234
// 456D             qwer
// 789E             asdf
// A0BF             zxcv
void handle_input(chip8_t *chip8){
    SDL_Event event;
    while(SDL_PollEvent(&event)){
        switch(event.type){
            case SDL_QUIT:
                // Exit window; End program
                chip8->state = QUIT; // Will exit main loop
                return;

            case SDL_KEYDOWN:
                switch(event.key.keysym.sym){
                    case SDLK_ESCAPE:
                        // Escape key;
                        chip8->state = QUIT;
                        return;
                    case SDLK_SPACE:
                        // Pause/Unpause emulator
                        if(chip8->state == RUNNING)
                            chip8->state = PAUSED;
                        else{
                            chip8->state = RUNNING;
                            puts("==== PAUSED ====");
                        }
                        return;

                    // Map qwrert keys to CHIP8 keypad
                    case SDLK_1: chip8->keypad[0x1] = true; break;
                    case SDLK_2: chip8->keypad[0x2] = true; break;
                    case SDLK_3: chip8->keypad[0x3] = true; break;
                    case SDLK_4: chip8->keypad[0xC] = true; break;

                    case SDLK_q: chip8->keypad[0x4] = true; break;
                    case SDLK_w: chip8->keypad[0x5] = true; break;
                    case SDLK_e: chip8->keypad[0x6] = true; break;
                    case SDLK_r: chip8->keypad[0xD] = true; break;

                    case SDLK_a: chip8->keypad[0x7] = true; break;
                    case SDLK_s: chip8->keypad[0x8] = true; break;
                    case SDLK_d: chip8->keypad[0x9] = true; break;
                    case SDLK_f: chip8->keypad[0xE] = true; break;

                    case SDLK_z: chip8->keypad[0xA] = true; break;
                    case SDLK_x: chip8->keypad[0x0] = true; break;
                    case SDLK_c: chip8->keypad[0xB] = true; break;
                    case SDLK_v: chip8->keypad[0xF] = true; break;

                    default: break;
                }
                break;

            case SDL_KEYUP:
                switch(event.key.keysym.sym){

                    // Map qwerty keys to CHIP8 keypad
                    case SDLK_1: chip8->keypad[0x1] = false; break;
                    case SDLK_2: chip8->keypad[0x2] = false; break;
                    case SDLK_3: chip8->keypad[0x3] = false; break;
                    case SDLK_4: chip8->keypad[0xC] = false; break;

                    case SDLK_q: chip8->keypad[0x4] = false; break;
                    case SDLK_w: chip8->keypad[0x5] = false; break;
                    case SDLK_e: chip8->keypad[0x6] = false; break;
                    case SDLK_r: chip8->keypad[0xD] = false; break;

                    case SDLK_a: chip8->keypad[0x7] = false; break;
                    case SDLK_s: chip8->keypad[0x8] = false; break;
                    case SDLK_d: chip8->keypad[0x9] = false; break;
                    case SDLK_f: chip8->keypad[0xE] = false; break;

                    case SDLK_z: chip8->keypad[0xA] = false; break;
                    case SDLK_x: chip8->keypad[0x0] = false; break;
                    case SDLK_c: chip8->keypad[0xB] = false; break;
                    case SDLK_v: chip8->keypad[0xF] = false; break;

                    default: break;
                }
            break;
            default:
                break;
        }
    }
}

#ifdef DEBUG
void print_debug_info(chip8_t *chip8){
    printf("Address: 0x%04X, Opcode: 0x%04X Desc: ",chip8->PC-2, chip8->inst.opcode);

    // Emulate opcode
    switch((chip8->inst.opcode >> 12) & 0x0F){
        case 0x00:
            if(chip8->inst.NN == 0xE0){
                // 0x00E0: Clear screen
                printf("Clear screen\n");
            }
            else if(chip8->inst.NN == 0xEE){
                // 0x00EE: Return from subroutine
                // set progrma address to last address from subroutine stack ("pop" it off the stack)
                // so that next opcode will be gotten from address.
                printf("Return from subroutine to address 0x%04X \n", *(chip8->stack_ptr - 1));
            }
            else{
                printf("Unimplemented Opcode\n");
            }
            break;
        case 0x01:

            printf("Jump to address NNN (0x%04X)\n", chip8->inst.NNN);
            break;
        case 0x02:
            // 0x2NNN: Call subroutine at NNN
            // Store current address to return to on subroutine stack ("push" iton the stack)
            // and set program counter to subroutine address so that the next opcode
            // is gotten from there.

            *chip8->stack_ptr++ = chip8->PC; 
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x03:
            printf("Check if V%X (0x%02X) == NN (0x%02X), skip next instruction if true\n",  chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
            break;
        case 0x04:
            printf("Check if V%X (0x%02X) != NN (0x%02X), skip next instruction if true\n",  chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
            break;
        case 0x05:
            printf("Check if V%X (0x%02X) == V%X (0x%02X), skip next instruction if true\n",  chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y]);
            break;
        case 0x06:
            // 0x6XNN: Set register V[X] to NN
            printf("Set register V[%X] to NN (0x%02X)\n", chip8->inst.X, chip8->inst.NN);
            break;
        case 0x0A:
            // 0xANNN: Set I to NNN
            printf("Set I to NNN (0x%04X)\n", chip8->inst.NNN);
            break;
        case 0x0D:
            printf("Draw N (%u) height sprite at coords V%X (0x%02X), V%X (0x%02X) from memory location I (0x%04X). Set VF = 1 if any pixels are turned off\n", 
                chip8->inst.N, chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y], chip8->I);
            break;
        case 0x07:
            // 0x6XNN: Set register V[X] to NN
            printf("Set register V%X to NN (0X%02X) += NN (0X%02X). Result: 0X%02X\n", 
                chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN, chip8->V[chip8->inst.X] + chip8->inst.NN);
            break;
        case 0x08:
            switch(chip8->inst.N){
                case 0:
                    // 0x8XY0: Set register VX = VY
                    printf("Set register V%X = V%X (0X%02X)\n", 
                        chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y]);
                    break;
                case 1:
                    // 0x8XY1: Set register VX |= VY
                    printf("Set register V%X (0x%02X) |= V%X (0X%02X); Result: 0X%02X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->inst.Y, chip8->V[chip8->inst.Y],
                        chip8->V[chip8->inst.X] | chip8->V[chip8->inst.Y]);
                    break;
                case 2:
                    // 0x8XY2: Set register VX &= VY
                    printf("Set register V%X (0x%02X) &= V%X (0X%02X); Result: 0X%02X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->inst.Y, chip8->V[chip8->inst.Y],
                        chip8->V[chip8->inst.X] & chip8->V[chip8->inst.Y]);
                    break;
                case 3:
                    // 0x8XY3: Set register VX ^= VY
                    printf("Set register V%X (0x%02X) ^= V%X (0X%02X); Result: 0X%02X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->inst.Y, chip8->V[chip8->inst.Y],
                        chip8->V[chip8->inst.X] ^ chip8->V[chip8->inst.Y]);
                    break;
                case 4:
                    // 0x8XY4: Set register VX += VY
                    printf("Set register V%X (0x%02X) += V%X (0X%02X), VF = 1 if carry; Result: 0X%02X, VF = %X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->inst.Y, chip8->V[chip8->inst.Y],
                        chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y],
                        ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255));
                    break;
                case 5:
                    // 0x8XY5: Set register VX -= VY
                    printf("Set register V%X (0x%02X) -= V%X (0X%02X), VF = 1 if no borrow; Result: 0X%02X, VF = %X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->inst.Y, chip8->V[chip8->inst.Y],
                        chip8->V[chip8->inst.X] - chip8->V[chip8->inst.Y],
                        (chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]));
                    break;
                case 6:
                    // 0x8XY6: Set register VX >>= 1, store shifted off bit in VF
                    printf("Set register V%X (0x%02X) >>= 1, VF = shifted off bit (%X); Result 0X%02X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->V[chip8->inst.X] & 1,
                        chip8->V[chip8->inst.X] >> 1);
                    break;
                case 7:
                    // 0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive)
                    printf("Set register V%X = V%X (0x%02X) - V%X (0X%02X), VF = 1 if no borrow; Result: 0X%02X, VF = %X\n", 
                        chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.X],
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X],
                        (chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]));
                    break;
                case 0xE:
                    // 0x8XYE: Set register VX <<= 1, store shifted off bit in VF
                    printf("Set register V%X (0x%02X) <<= 1, VF = shifted off bit (%X); Result 0X%02X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->V[chip8->inst.X] & (0x08) >> 7,
                        chip8->V[chip8->inst.X] << 1);
                    break;
                default:
                    // Wrong/unimplemented opcode
                    break;
            }
            break;
        case 0x09:
            // 0x9XY0: Skip next instruction if VX != VY
            printf("Set register V%X (0x%02X) != V%X (0X%02X), skip next instruction if true\n", 
                chip8->inst.X, chip8->V[chip8->inst.X],
                chip8->inst.Y, chip8->V[chip8->inst.Y]);
            break;
        case 0x0B:
            // 0xBNNN: Jump to address v0 + NNN
            printf("Set PC to V0 (0x%02X) + NNN (0x%04X); Result PC = 0x%04X\n",
                chip8->V[0], chip8->inst.NNN, chip8->V[0] + chip8->inst.NNN);

            break;
        case 0x0C:
            // 0xCXNN: Sets register VX = rand() % 256 & NN (bitwise AND)
            printf("Set V%X = rand() %% 256 & NN (0x%02X)\n",
                chip8->inst.X, chip8->inst.NN);

            break;
        case 0x0E:
            if(chip8->inst.NN == 0x9E){
                // 0xEX9E: Skip next instruction if key in VX is pressed
                printf("Skip next instruction if key in V%X (0x%02X) is pressed; Keypad value: %d\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);
            }
            else if(chip8->inst.NN == 0xA1){
                printf("Skip next instruction if key in V%X (0x%02X) is not pressed; Keypad value: %d\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);
            }
            break;
        case 0x0F:
            switch(chip8->inst.NN){
                case 0x0A:
                    // 0xFX0A: VX = get_key(): Await until a keypress, an store in VX
                    printf("Await until a key is pressed; Store key in V%X\n", chip8->inst.X);
                    break;

                case 0x1E:
                    // 0xFX1E: I += VX; Add VX to register I. For non-Aniga CHIP8, does not affect VF
                    printf("I (0x%04X) += V%X (0x%02X) Result (I): 0x%04X\n",
                        chip8->I, chip8->inst.X, chip8->V[chip8->inst.X], chip8->I + chip8->V[chip8->inst.X]);
                    break;
                case 0x07:
                    // 0xFX07: Set VX to delay timer value
                    printf("Set V%X = delay timer value (0x%02X)\n", chip8->inst.X, chip8->delay_timer);
                    break;
                case 0x15:
                    // 0xFX15: Set delay timer to VX
                    printf("Set delay timer = V%X (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X]);
                    break;
                case 0x18:
                    // 0xFX18: Set sound timer to VX
                    printf("Set delay timer = V%X (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X]);
                    break;
                case 0x29:
                    // 0xFX29: Set register I to location of sprite for digit VX
                    printf("Set I to sprite location in memory for characters in V%X (0x%02X). Result(VX*5) = (0x%02X) \n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->V[chip8->inst.X] * 5);
                    break;
                case 0x33:
                    // 0xFX33: Store BCD representation of VX in memory locations I, I+1, I+2
                    printf("Store BCD representation of V%X (0x%02X) in memory locations I (0x%04X), I+1, I+2\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
                    break;
                case 0x55:
                    // 0xFX55: Store V0 to VX in memory starting at I
                    printf("Register dump V0-V%X (0x%02X) inclusive at memory from I (0x%04x)\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
                    break;
                case 0x65:
                    // 0xFX65: Store V0 to VX in memory starting at I
                    printf("Register load V0-V%X (0x%02X) inclusive at memory from I (0x%04x)\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
                    break;
                default:
                    // Wrong/unimplemented opcode
                    break;
            }
            break;

        default:
            printf("Unimplemented Opcode\n");
            break;
    }
    
}
#endif

// Emulate 1 CHIP8 instruction
void emulate_instructions(chip8_t *chip8, const config_t config){
    // Get next opcode from ram
    chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC + 1];
    chip8->PC += 2; // Pre-increment program counter for next opcode

    // Fill out instruction format
    //chip8->inst.category = (chip8->inst.opcode >> 12) & 0x0F;
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x0FF;
    chip8->inst.N = chip8->inst.opcode & 0x0F;
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

#ifdef DEBUG
    print_debug_info(chip8);
#endif

    // Emulate opcode
    switch((chip8->inst.opcode >> 12) & 0x0F){
        case 0x00:
            if(chip8->inst.NN == 0xE0){
                // 0x00E0: Clear screen
                memset(&chip8->display[0], false, sizeof chip8->display);

            }
            else if(chip8->inst.NN == 0xEE){
                // 0x00EE: Return from subroutine
                // set progrma address to last address from subroutine stack ("pop" it off the stack)
                // so that next opcode will be gotten from address.
                chip8->PC = *--chip8->stack_ptr;
            }
            else {
                // Unimplemented invalid code, may be 0xNNN fro calling machine code routine for RCA1802
            }
            break;
        case 0x01:
            // 0x1NNN jumps to address NNN
            chip8->PC = chip8->inst.NNN; // Set program counter so that next opcode is from NNN
            break;
        case 0x02:
            // 0x2NNN: Call subroutine at NNN
            // Store current address to return to on subroutine stack ("push" iton the stack)
            // and set program counter to subroutine address so that the next opcode
            // is gotten from there.

            *chip8->stack_ptr++ = chip8->PC; 
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x03:
            // 0x3XNN: Check if VX == NN, if so, skip the next instruction
            if(chip8->V[chip8->inst.X] == chip8->inst.NN){
                chip8->PC += 2; // Skip next opcode/instruction
            } 
            break;
        case 0x04:
            // 0x4XNN: Check if VX != NN, if so, skip the next instruction
            if(chip8->V[chip8->inst.X] != chip8->inst.NN){
                chip8->PC += 2; // Skip next opcode/instruction
            } 
            break;
        case 0x05:
            if(chip8-> inst.N != 0) break; // Wrong opcode

            // 0x5XY0: Check if VX == VY, if so, skip the next instruction
            if(chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]){
                chip8->PC += 2; // Skip next opcode/instruction
            } 
            break;
        case 0x06:
            // 0x6XNN: Set register VX to NN
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;
        case 0x07:
            // 0x6XNN: Set register VX += NN
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;
        case 0x08:
            switch(chip8->inst.N){
                case 0:
                    // 0x8XY0: Set register VX = VY
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
                    break;
                case 1:
                    // 0x8XY1: Set register VX |= VY
                    chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                    break;
                case 2:
                    // 0x8XY2: Set register VX &= VY
                    chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                    break;
                case 3:
                    // 0x8XY3: Set register VX ^= VY
                    chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                    break;
                case 4:
                    // 0x8XY4: Set register VX += VY
                    if ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255){
                        chip8->V[0xF] = 1;
                    }

                    chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                    break;
                case 5:
                    // 0x8XY5: Set register VX -= VY
                    //if (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]){
                    //    chip8->V[0xF] = 1;
                    //}

                    chip8->V[0xF] = (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]);
                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                    break;
                case 6:
                    // 0x8XY6: Set register VX >>= 1, store shifted off bit in VF
                    chip8->V[0xF] = chip8->V[chip8->inst.X] & 1;
                    chip8->V[chip8->inst.X] >>= 1;
                    break;
                case 7:
                    // 0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive)
                    //if (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]){
                    //    chip8->V[0xF] = 1;
                    //}

                    chip8->V[0xF] = (chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]);
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    break;
                case 0xE:
                    // 0x8XYE: Set register VX <<= 1, store shifted off bit in VF
                    chip8->V[0xF] = (chip8->V[chip8->inst.X] & 0x80) >> 7;
                    chip8->V[chip8->inst.X] <<= 1;
                    break;
                default:
                    // Wrong/unimplemented opcode
                    break;
            }
            break;
        case 0x09:
            // 0x9XY0: Skip next instruction if VX != VY
            if(chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y]){
                chip8->PC += 2;
            }
            break;
        case 0x0A:
            // 0xANNN: Set I to NNN
            chip8->I = chip8->inst.NNN;
            break;
        case 0x0B:
            // 0xBNNN: Jump to address v0 + NNN
            chip8->PC = chip8->V[0] + chip8->inst.NNN;

            break;
        case 0x0C:
            // 0xCXNN: Sets register VX = rand() % 256 & NN (bitwise AND)
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;

            break;
        case 0x0D:
            // 0xDXYN: Draw N height sprite coors X, Y; Read from memory location I;
            // Screen pixels are XOR'd with sprite bits,
            // VF (Carry flag) is set if any screen pixels are set off; this is usefull
            // for collision detection or other reasons.

            uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
            uint8_t Y_coord = chip8->V[chip8->inst.Y] % config.window_height;
            const uint8_t orig_X = X_coord; // Original X Value
            chip8->V[0xF] = 0; // Initialize carry flag to 0

            // Loop over all N rows of the sprite
            for(uint8_t i = 0; i < chip8->inst.N; i++){
                // Get next byte/row of sprite data
                const uint8_t sprite_data = chip8->ram[chip8->I + i];
                X_coord = orig_X; // Reste X for next row to draw

                for(int8_t j = 7; j >= 0; j--){
                    // If sprite pixek/bit is on and display pixel is on, set carry flag
                    //bool *pixel = &chip8->display[Y_coord * config.window_height + X_coord];
                    bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord];
                    const bool sprite_bit = (sprite_data & (1 << j));
                    if(sprite_bit && *pixel){
                        chip8->V[0xF] = 1;
                    }

                    // XOR display pixel with sprite pixel/bit to on or off
                   *pixel ^= sprite_bit;

                   // Stop drawing if hit right edge of screen
                    if(++X_coord >= config.window_width) break;
                }
                if(++Y_coord >= config.window_height) break;
            }
            break;
        case 0x0E:
            if(chip8->inst.NN == 0x9E){
                // 0xEX9E: Skip next instruction if key in VX is pressed
                if(chip8->keypad[chip8->V[chip8->inst.X]] == true)
                    chip8->PC += 2;
            }
            else if(chip8->inst.NN == 0xA1){
                if(!chip8->keypad[chip8->V[chip8->inst.X]])
                    chip8->PC += 2;
            }
            break;
        case 0x0F:
            switch(chip8->inst.NN){
                case 0x0A:
                    // 0xFX0A: VX = get_key(): Await until a keypress, an store in VX
                    bool any_key_pressed = false;
                    for(uint8_t i = 0; i < sizeof chip8->keypad; i++){
                        if(chip8->keypad[i]){
                            chip8->V[chip8->inst.X] = i; // i = key (offset into keypad array)
                            any_key_pressed = true;
                            break;
                        }
                    }

                    // Keep gettinh the current opcode nad running this instruction
                    if(!any_key_pressed){
                        chip8->PC -= 2; 
                    }
                    break;

                case 0x1E:
                    // 0xFX1E: I += VX; Add VX to register I. For non-Aniga CHIP8, does not affect VF
                    chip8->I += chip8->V[chip8->inst.X];
                    break;
                case 0x07:
                    // 0xFX07: Set VX to delay timer value
                    chip8->V[chip8->inst.X] = chip8->delay_timer;
                    break;
                case 0x15:
                    // 0xFX15: Set delay timer to VX
                    chip8->delay_timer = chip8->V[chip8->inst.X];
                    break;
                case 0x18:
                    // 0xFX18: Set sound timer to VX
                    chip8->sound_timer = chip8->V[chip8->inst.X];
                    break;
                case 0x29:
                    // 0xFX29: Set register I to location of sprite for digit VX
                    chip8->I = chip8->V[chip8->inst.X] * 5; // Each sprite is 5 bytes long
                    break;
                case 0x33:
                    // 0xFX33: Store BCD representation of VX in memory locations I, I+1, I+2
                    uint8_t bcd = chip8->V[chip8->inst.X];
                    chip8->ram[chip8->I + 2] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I + 1] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I] = bcd;
                    break;
                case 0x55:
                    // 0xFX55: Store V0 to VX in memory starting at I
                    // NOTE: Could make this a config flag to use SCHHIP or CHIP8 behavior for I
                    for(uint8_t i = 0; i <= chip8->inst.X; i++){
                        chip8->ram[chip8->I + i] = chip8->V[i];
                    }
                    break;
                case 0x65:
                    // 0xFX65: Fill V0 to VX with values from memory starting at I
                    // NOTE: Could make this a config flag to use SCHHIP or CHIP8 behavior for I
                    for(uint8_t i = 0; i <= chip8->inst.X; i++){
                        chip8->V[i] = chip8->ram[chip8->I + i];
                    }
                    break;
                default:
                    // Wrong/unimplemented opcode
                    break;
            }
            break;
        default:
            break;
    }
}

void update_timers(const sdl_t sdl, chip8_t *chip8){
    if(chip8->delay_timer > 0){
        chip8->delay_timer--;
    }

    if(chip8->sound_timer > 0){
        chip8->sound_timer--;
        SDL_PauseAudioDevice(sdl.dev, 0); // Play sound
    }
    else{
        SDL_PauseAudioDevice(sdl.dev, 1); // Stop sound
    }
}

// Main function
int main(int argc, char **argv){
    // Default Usage message for args
    if(argc < 2){
        fprintf(stderr, "Usage: %s <rom_name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //puts("TESTING CHIP 8");

    // Init emulator configuration
    config_t config = {0};
    if(!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);

    const char *rom_name = argv[1];

    // Initialize SDL
    sdl_t sdl = {0};
    if(!init_sdl(&sdl, &config, rom_name)) exit(EXIT_FAILURE);

    // Initialize CHIP8 machine
    chip8_t chip8 = {0};
    
    if(!init_chip8(&chip8, rom_name)) exit(EXIT_FAILURE);

    // Initial screen clear
    clear_screen(sdl, config);

    // Seed random number generator
    srand(time(NULL));
    
    // Main emulator loop
    while(chip8.state != QUIT){
        // Handle user input
        handle_input(&chip8);

        if(chip8.state == PAUSED) continue;

        // get time before running instructions
        const uint64_t start_frame_time = SDL_GetPerformanceCounter();

        // Get_time();

        // Emulate CHIP8 Instructions for this emulator "frame" (60 hz)
        for(uint32_t i = 0; i < config.insts_per_second / 60; i++){
            emulate_instructions(&chip8, config);
        }

        // get time elapsed since running instructions
        const uint64_t end_frame_time = SDL_GetPerformanceCounter();

        // Emulate CHIP8 instructions
        // Get_time(); elapsed since last get_time();

        // Delay for 60hz
        const double time_elapsed = (double)((end_frame_time - start_frame_time) * 1000) / SDL_GetPerformanceFrequency();
        SDL_Delay(16.67f > time_elapsed ? 16.67f - time_elapsed : 0);

        update_screen(sdl, config, chip8);
        update_timers(sdl, &chip8);
    }

    // Final cleanup
    final_cleanup(sdl);

    exit(EXIT_SUCCESS);
}