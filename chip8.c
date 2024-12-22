#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<stdbool.h>
#include<string.h>
#include<time.h>

//#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h" //#include "SDL.h" 

// SDL Container object
typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

// Emulator configuation object
typedef struct
{
    uint32_t window_width;  // SDL Window width
    uint32_t window_height; // SDL Window height
    uint32_t fg_color;      // foreground RGBA8888
    uint32_t bg_color;      // background RGBA8888
    uint32_t scale_factor;  // Amount to scale a chip8 pixel by e.g. 20x will be a 20x larger window
    bool pixel_outlines;    // Draw pixel "outlines" yes/no
} config_t;

// Emulator states
typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

// CHIP8 instruction format
typedef struct {
    uint16_t opcode;
    uint16_t NNN;   //12 bit address/constant
    uint8_t NN;     //8 bit constant
    uint8_t N;      //4 bit constant
    uint8_t X;      //4 bit register identifier
    uint8_t Y;      //4 bit register identifier
} instruction_t;

// CHIP8 Machine object
typedef struct {
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64*32];    // uint8_t *display; // display = &ram[0xF00]; display; Emulate original CHIP8 resolution pixels
    uint16_t stack[12];     // Subroutine stack
    uint16_t *stack_ptr;
    uint8_t V[16];           // Data registrews V0-VF; uint8_t V[0x10]
    uint16_t I;             // Index register
    uint16_t PC;            // Program counter
    uint8_t delay_timer;    // Decrements at 60hz when >0
    uint8_t sound_timer;    // Decrements at 60hz and plays tone when >0
    bool keypad[16];        // Hexadecimal keypad 0x0-0xF
    const char *rom_name;         // Currently running ROM
    instruction_t inst;     // Currently executing instruction
} chip8_t;

// Initialize SDL
bool init_sdl(sdl_t *sdl, const config_t config, const char rom_name[]){
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("Could not initialize SDL subsystems! %s\n", SDL_GetError());
        return false;
    }

    // Window title
    char window_title[128] = "CHIP8 Emulator - ";
    strcat(window_title, rom_name);

    sdl -> window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   config.window_width * config.scale_factor,
                                   config.window_height * config.scale_factor, 
                                   0);
    if (!sdl->window) {
        SDL_Log("Could not create SDL window %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl->renderer) {
        SDL_Log("Could not create SDL renderer %s\n", SDL_GetError());
        return false;
    }

    return true;
}

// Set up initial emulator config from passed in arguments
bool set_config_from_args(config_t *config, const int argc, char **argv) {

    // Set defaults
    *config = (config_t){
        .window_width = 64, // Original x resolution
        .window_height = 32,
        .fg_color = 0xFFFFFFFF, // WHITE
        .bg_color = 0x000000FF, // BLACK
        .scale_factor = 20,     // Default resolution will ve 1280x640
        .pixel_outlines = true, // Draw pixel "outlines" by default

    };

    // Override defaults from passed in arguments
    for (int i = 1; i < argc; i++) {
        (void)argv[i]; // Prevent compiler error from unused variables argc/argv
        // ..
    }

    return true; 
}

// Initialize CHIP* machine
bool init_chip8(chip8_t *chip8, const char rom_name[]) {
    const uint32_t entry_point = 0x200; // CHIP8 Roms will be loaded to 0x200
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

    // Open rom file
    FILE *rom = fopen(rom_name, "rb");
    if (!rom) {
        SDL_Log("Rom file %s is invalid or does not exist\n", rom_name);
        return false;
    }

    // Get/check rom size
    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof chip8->ram - entry_point;
    rewind(rom);

    if (rom_size > max_size) {
        SDL_Log("Rom file %s is too big! Rom size: %zu, Max size allowed: %zu\n",
                rom_name, rom_size, max_size);
        return false;
    }

    if (fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1) {
        SDL_Log("Culd not read Rom file %s into CHIP8 memory\n!", rom_name);
    }

    fclose(rom);

    // Set chip8 machine defaults
    chip8->state = RUNNING;     // Default machine state to on/run
    chip8->PC = entry_point;    // Start program counter at ROM entry point
    chip8->rom_name = rom_name; //
    chip8->stack_ptr = &chip8->stack[0]; 

    return true; // Success
}

void final_cleanup(const sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}

void clear_screen(const sdl_t sdl, const config_t config) {
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >>  8) & 0xFF;
    const uint8_t a = (config.bg_color >>  0) & 0xFF;

    SDL_SetRenderDrawColor(sdl.renderer, r ,g ,b ,a);
    SDL_RenderClear(sdl.renderer);
}

// Update window with any changes
void update_screen(const sdl_t sdl, const config_t config, const chip8_t chip8) {
    SDL_Rect rect = {.x = 0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};
    // Grab color values to draw
    const uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
    const uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
    const uint8_t fg_b = (config.fg_color >>  8) & 0xFF;
    const uint8_t fg_a = (config.fg_color >>  0) & 0xFF;

    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    const uint8_t bg_b = (config.bg_color >>  8) & 0xFF;
    const uint8_t bg_a = (config.bg_color >>  0) & 0xFF;

    // Loop through display pixels, draw a rectangle per pixel to the SDL window
    for (uint32_t i = 0; i < sizeof chip8.display; i++) {
        // Translate 1D index i value to 2d X/Y coordinates
        // X = 1 % window width
        // Y = i / window width
        rect.x = (i % config.window_width) * config.scale_factor;
        rect.y = (i / config.window_width) * config.scale_factor;

        if (chip8.display[i]) {
            // Pixel is on, draw foreground color
            SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);

            // if user requested drawing pixel outlines, draw those here
            if (config.pixel_outlines) {
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }

        } else {
            // Pixel is off, draw background color
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);

        }
    }
    
    SDL_RenderPresent(sdl.renderer);
}

// Handle user input
// CHIP8 Keypad     QWERTY
/*
123C                1234
456D                qwer
789E                asdf
A0BF                zxcv
*/
void handle_input(chip8_t *chip8) {
    SDL_Event event;

    while (SDL_PollEvent(&event)){
        switch (event.type) {
            case SDL_QUIT:
                // exit window; End Program
                chip8->state = QUIT;
                return;

            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        // ESCAPE key; exit window & End program
                        chip8->state = QUIT;
                        return;
                    
                    case SDLK_SPACE:
                        // Space bar
                        if (chip8->state == RUNNING)
                            chip8->state = PAUSED;
                        else {
                            chip8->state = RUNNING;
                            puts("===== Paused =====");
                        }
                        return;

                    // Map querty keys to CHIP8 keypad                        
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
                
                switch(event.key.keysym.sym) {
                // Map querty keys to CHIP8 keypad                        
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
void print_debug_info(chip8_t *chip8) {
    printf("Address: %04X, Opcode: 0x%04X Desc: ",
            chip8->PC-2, chip8->inst.opcode);

    switch ((chip8->inst.opcode >> 12) & 0x0F) {
        case 0x00:
            if (chip8->inst.NN == 0xE0) {
                // 0x00E0: Clear the screen
                printf("Clear screen\n");

            } else if (chip8->inst.NN == 0xEE) {
                // 0x00EE: Return from subroutine
                // Set program counter to last address on subroutine stack ("pop" it off the stack)
                //  so that next opcode will be gotten from that address.
                printf("Return from subroutine to address 0x%04X\n",
                       *(chip8->stack_ptr - 1));
            } else {
                printf("Unimplemented Opcode.\n");
            }
            break;
        
        case 0x01:
            // 0X1NNN: Jump to address NNN
            printf("Jump to address NNN (0x%04X)\n",
                chip8->inst.NNN);
            break;

        case 0x02:
            // 0x2NNN: Call subroutine at NNN
            // Store current address to return to on subroutine stack ("push" int on the stack)
            //  and set program counter to subroutine address so that the next opcode
            //  is gotten from there.
            *chip8->stack_ptr++ = chip8->PC;
            chip8->PC = chip8->inst.NNN;
            break;

        case 0x03:
            // 0x3XNN: Chek if VX == NN, if so, skip the next instruction
            printf("Check if V%X (0x%02X) == NN (0x%02X), skip next instruction if true\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
            break;

        case 0x04:
            // 0x4XNN: Check if VX != NN, if so, skip the next instruction
            printf("Check if V%X (0x%02X) != NN (0x%02X), skip next instruction if true\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
            break;

        case 0x05:

            printf("Check if V%X (0x%02X) == V%X (0x%02X), skip next instruction if true\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], 
                    chip8->inst.Y, chip8->V[chip8->inst.Y]);
            break;

        case 0x06:
            // 0x6XNN: Set register VX to NN
            printf("Set register V%X = NN (0x%02X)\n", chip8->inst.X, chip8->inst.NN);
            break;

        case 0x07:
            // 0x7XNN: Set register VX += NN
            printf("Set register V%X (0x%02X) += NN (0x%02X). Result: 0x%02X\n",
            chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN,
            chip8->V[chip8->inst.X] + chip8->inst.NN);
            break;

        case 0x08:
            switch(chip8->inst.N) {
                case 0:
                    // 0x8XY0: Set register VX = VY
                    printf("Set register V%X = V%X (0x%02X)\n",
                            chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y]);
                    break;
                
               case 1:
                    // 0x8XY1: Set register VX |= VY
                    printf("Set register V%X (0x%02X) != V%X (0x%02X); Result: 0x%02X\n",
                            chip8->inst.X, chip8->V[chip8->inst.X],
                            chip8->inst.Y, chip8->V[chip8->inst.Y],
                            chip8->V[chip8->inst.X] | chip8->V[chip8->inst.Y]);
                    break;

               case 2:
                    // 0x8XY2: Set register VX &= VY
                    printf("Set register V%X (0x%02X) &= V%X (0x%02X); Result: 0x%02X\n",
                            chip8->inst.X, chip8->V[chip8->inst.X],
                            chip8->inst.Y, chip8->V[chip8->inst.Y],
                            chip8->V[chip8->inst.X] & chip8->V[chip8->inst.Y]);
                    break;

               case 3:
                    // 0x8XY3: Set register VX ^= VY
                    printf("Set register V%X (0x%02X) ^= V%X (0x%02X); Result: 0x%02X\n",
                            chip8->inst.X, chip8->V[chip8->inst.X],
                            chip8->inst.Y, chip8->V[chip8->inst.Y],
                            chip8->V[chip8->inst.X] ^ chip8->V[chip8->inst.Y]);
                    break;
                
               case 4:
                    // 0x8XY4: Set register VX += VY, set VF to 1 if carry
                    printf("Set register V%X (0x%02X) += V%X (0x%02X), VF = 1 if carry; Result: 0x%02X, VF = %X\n",
                            chip8->inst.X, chip8->V[chip8->inst.X],
                            chip8->inst.Y, chip8->V[chip8->inst.Y],
                            chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y],
                            ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255));
                    break;

                case 5:
                    // 0x8XY5: Set register VX -= VY, set VF to 1 if there is not a borrow (result is positive)
                    printf("Set register V%X (0x%02X) -= V%X (0x%02X), VF = 1 if no borrow; Result: 0x%02X, VF = %X\n",
                            chip8->inst.X, chip8->V[chip8->inst.X],
                            chip8->inst.Y, chip8->V[chip8->inst.Y],
                            chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y],
                            (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]));
                    break;

                case 6:
                    // 0x8XY6: Set register VX >>= 1, store shifted off bit in VF
                    printf("Set register V%X (0x%02X) >>= 1, VF = shifted off bit (%X); Result: 0x%02X\n",
                            chip8->inst.X, chip8->V[chip8->inst.X],
                            chip8->V[chip8->inst.X] & 1,
                            chip8->V[chip8->inst.X] >> 1);
                    break;

                case 7:
                    // 0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive) 
                    printf("Set register V%X = V%X (0x%02X) - V%X (0x%02X), VF = 1 if no borrow; Result: 0x%02X, VF = %X\n",
                            chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y],
                            chip8->inst.X, chip8->V[chip8->inst.X],
                            chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X],
                            (chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]));
                    break;
                
                case 0xE:
                    // 0x8XY6: Set register VX >>= 1, store shifted off bit in VF
                    printf("Set register V%X (0x%02X) <<= 1, VF = shifted off bit (%X); Result: 0x%02X\n",
                            chip8->inst.X, chip8->V[chip8->inst.X],
                            (chip8->V[chip8->inst.X] & 0x80) >> 7,
                            chip8->V[chip8->inst.X] << 1);
                    break;


                default:
                    // Wrong/unimplemented opcode
                    break;
            }
            break;

        case 0x09:
            // 0x9XY0: Check if VX != VY; Skip next instruction if so
            printf("Check if V%X (0x%02X) != V%X (0x%02X), skip next instruction if true\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], 
                    chip8->inst.Y, chip8->V[chip8->inst.Y]);
            break;

        case 0x0A:
            // 0xANNN: Set index register I to NNN
            printf("Set I to NNN (0x%04X)\n",
                   chip8->inst.NNN);
            break;

        case 0x0B:
            // 0xBNNN: Jump to V0 + NNN
            printf("Set PC to V0 (0x%02X) + NNN (0x%04X); Result PC = 0x%04X\n",
                chip8->V[0], chip8->inst.NNN, chip8->V[0] + chip8->inst.NNN);
            break;

        case 0x0C:
            // 0xCXNN: Set register VX = rand() % 256 & NN (bitwise AND)
            printf("Set V%X = rand() %% 256 & NN (0x%02X)\n",
                   chip8->inst.X, chip8->inst.NNN);
            chip8->V[chip8->inst.X] = rand() % 256 & chip8->inst.NN;
            break;

        case 0x0D:
            printf("Draw N (%u) height sprite at coords V%X (0x%02x) , V%X (0x%02X) from memory location I (0x%04X). Set VF - 1 if any pixels are turned off.",
            chip8->inst.N, chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y,
            chip8->V[chip8->inst.Y], chip8->I);
            break;

        case 0x0E:
            if (chip8->inst.NN == 0x9E) {
                // 0xEX9E: Skip next instruction if key in VX is pressed
                printf("Skip next intruction if key in V%X (0x%02X) is pressed; Keypad value: %d\n",
                        chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);
                        
            } else if (chip8->inst.NN == 0xA1) {
                // 0xEX9E: Skip next instruction if key in VX is not pressed
                printf("Skip next intruction if key in V%X (0x%02X) is not pressed; Keypad value: %d\n",
                        chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);
            }
            break;

        default:
            printf("Unimplemented Opcode.\n");
            break; // Uniplementeded or ivalid opcode
    }
}
#endif

// Emulate 1 CHIP8 instruction
void emulate_instruction(chip8_t *chip8, const config_t config) {
    // Get next opcode from ROM/ram
    chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC+1];
    chip8->PC += 2; // Pre-increment program counter for next opcode 

    // Fill out current instruction format
    // DXYN
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
    switch ((chip8->inst.opcode >> 12) & 0x0F) {
        case 0x00:
            if (chip8->inst.NN == 0xE0) {
                // 0x00E0: Clear the screen
                memset(&chip8->display[0], false, sizeof chip8->display); 
            } else if (chip8->inst.NN == 0xEE) {
                // 0x00EE: Return from subroutine
                // Set program counter to last address on subroutine stack ("pop" it off the stack)
                //  so that next opcode will be gotten from that address.
                chip8->PC = *--chip8->stack_ptr;
            } else {
                // Unimplemented/invalid opcode, may be 0xNNN for calling machine code routine for RCA1802
            }

            break;

        case 0x01:
            // 0X1NNN: Jump to address NNN
            chip8->PC = chip8->inst.NNN; // Set program counter so that next opcode is from NNN
            break;

        case 0x02:
            // 0x2NNN: Call subroutine at NNN
            // Store current address to return to on subroutine stack ("push" int on the stack)
            //  and set program counter to subroutine address so that the next opcode
            //  is gotten from there.
            *chip8->stack_ptr++ = chip8->PC;
            chip8->PC = chip8->inst.NNN;
            break;

        case 0x03:
            // 0x3XNN: Check if VX == NN, if so, skip the next instruction
            if (chip8->V[chip8->inst.X] == chip8->inst.NN)
                chip8->PC += 2;     // Skip next opcode/instruction
            break;

        case 0x04:
            // 0x4XNN: Check if VX != NN, if so, skip the next instruction
            if (chip8->V[chip8->inst.X] != chip8->inst.NN)
                chip8->PC += 2;     // Skip next opcode/instruction
            break;

        case 0x05:
            // 0x5XY0 Check if VX == VY, if so skip the next instruction
            if (chip8->inst.N != 0) break; // Wrong opcode

            if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y])
                chip8->PC += 2;     // Skip next opcode/instruction
            break;

        case 0x06:
            // 0x6XNN: Set register VX to NN
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;

        case 0x07:
            // 0x7XNN: Set register VX += NN
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;
        
        case 0x08:
            switch(chip8->inst.N) {
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
                    // 0x8XY4: Set register VX += VY, set VF to 1 if carry
                    if ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255)
                        chip8->V[0xF] = 1;

                    chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                    break;

                case 5:
                    // 0x8XY5: Set register VX -= VY, set VF to 1 if there is not a borrow (result is positive)
                    if (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X])
                        chip8->V[0xF] = 1;

                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                    break;

                case 6:
                    // 0x8XY6: Set register VX >>= 1, store shifted off bit in VF
                    chip8->V[0xF] = chip8->V[chip8->inst.X] & 1;
                    chip8->V[chip8->inst.X] >>= 1;
                    break;

                case 7:
                    // 0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive) 
                    if (chip8->V[0xF] <= chip8->V[chip8->inst.X])
                        chip8->V[0xF] = 1;
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    break;
                
                case 0xE:
                    //0x8EXYE: Set register VX <<- 1, store shifted off bit in VF
                    chip8->V[0xF] = (chip8->V[chip8->inst.X] & 0x80) >> 7;
                    chip8->V[chip8->inst.X] <<= 1;

                default:
                    // Wrong/unimplemented opcode
                    break;
            }
            break;

        case 0x09:
            // 0x9XY0: Check if VX != VY; Skip next instruction if so
            if (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y])
                chip8->PC += 2;
            break;

        case 0x0A:
            // 0xANNN: Set index register I to NNN
            chip8->I = chip8->inst.NNN;
            break;

        case 0x0B:
            // 0xBNNN: Jump to V0 + NNN
            chip8->PC = chip8->V[0] + chip8->inst.NNN;
            break;

        case 0x0C:
            // 0xCXNN: Set register VX = rand() % 256 & NN (bitwise AND)
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
            break;

        case 0x0D:
            // 0XDXYN: Draw Nheight sprite at coords X,Y; Read from memory location I;
            //      screen pixels are XOR'd with sprite bits,
            //      VF (Carry flag) is set if any screen pixels are set off; This is useful
            //      for collision detection or other reasons.
            uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
            uint8_t Y_coord = chip8->V[chip8->inst.Y] % config.window_height;
            const uint8_t orig_X = X_coord; // Original X value

            chip8->V[0xF] = 0; // initialize carry flag to 0
            
            // Loop over all N rows of the sprite
            for (uint8_t i = 0; i < chip8->inst.N; i++) {
                // Get next byte/row of sprite data
                const uint8_t sprite_data = chip8->ram[chip8->I + i]; 
                X_coord = orig_X;   // Reset X for next row to draw

                for (int8_t j = 7; j >= 0; j--){
                    //if sprite pixel/bit is on and display pixel is on, set carry flag
                    //bool *pixel = &chip8->display[Y_coord * config.window_height + X_coord];
                    bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord]; // NEW
                    const bool sprite_bit = (sprite_data & (1 << j));
                    
                    if (sprite_bit && *pixel) {
                        chip8->V[0xF] = 1;
                    }
                    
                // XOR display pixel with sprite pixel/bit
                    *pixel ^= sprite_bit;

                    // Stop drawing if hit right edge of screen
                    if (++X_coord >= config.window_width) break;
                }
                // Stop drawing entire sprite
                if (++Y_coord >= config.window_height) break;
            }
            break;

        case 0x0E:
            if (chip8->inst.NN == 0x9E) {
                // 0xEX9E: Skip next instruction if key in VX is pressed
                if (chip8->keypad[chip8->V[chip8->inst.X]])
                    chip8->PC += 2;
            } else if (chip8->inst.NN == 0xA1) {
                // 0xEX9E: Skip next instruction if key in VX is not pressed
                if (!chip8->keypad[chip8->V[chip8->inst.X]])
                    chip8->PC += 2;
            }
            break;

        case 0x0F:
            switch (chip8->inst.NN) {
                case 0x0A:
                    // 0xFX0A: VX = get_key(); Await until a keypress, and store in VX
                    bool any_key_pressed = false;
                    for (uint8_t i = 0; i < sizeof chip8->keypad; i++) {
                        if (chip8->keypad[i]) {
                            chip8->V[chip8->inst.X] = i; // i = key (offset into keypad array)
                            break;
                        }
                    }
                
                // If no key has been pressed yet, keep getting the current opcode & running this instruction
                if (!any_key_pressed) chip8->PC -= 2;
                    
                break;
            }

        default:
            break; // Uniplementeded or ivalid opcode
    }
}

//Squeze
int main(int argc, char **argv) {
    // Dafault Usage message for args
    if (argc < 2) {
        fprintf(stderr, "Usage:  %s <rom_name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // Initialize emulator configuration/options
    config_t config = {0};
    if (!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);
    const char *rom_name = argv[1];

    // Initialize SDL
    sdl_t sdl = {0};
    if (!init_sdl(&sdl, config, rom_name)) exit(EXIT_FAILURE);

    // initialize chip8 machine
    chip8_t chip8 = {0};
    if (!init_chip8(&chip8, rom_name)) exit(EXIT_FAILURE);

    // Initial screen clear
    clear_screen(sdl, config);

    // Seed random number generator
    srand(time(NULL));

    // Main emulator loop
    while (chip8.state != QUIT){
        // handle user input
        handle_input(&chip8);

        if (chip8.state == PAUSED) continue;

        // Get_time();

        //handle_input();
        // Emulate chip8 instructions
        emulate_instruction(&chip8, config);

        // Get_time() elapsed since last get_time();
        // Delay for 60hz/60fps
        // SDL_Delay(16 - actual time);
        SDL_Delay(16);
        // Update window with changes
        update_screen(sdl, config, chip8);

    }

    // Final cleanup
    final_cleanup(sdl);
    exit(EXIT_SUCCESS);
}