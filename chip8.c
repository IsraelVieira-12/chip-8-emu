#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

//#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

// SDL Container object
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

// Emulator configuration object
typedef struct {
    uint32_t window_width; // SDL window width
    uint32_t window_height; // SDL window height
    uint32_t fg_color;  // Foreground Color RGBA8888
    uint32_t bg_color;  // Background Color RGBA8888
    uint32_t scale_factor; // Amount to scale a CHIP8 pixel by eg 20x will be 20x larger window
    bool pixel_outlines; // Draw pixel outlines yes/no
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

// Initialize SDL
bool init_sdl(sdl_t *sdl, const config_t config/* const char rom_name[]*/){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER ) != 0) {
        SDL_Log("Could not initialize SDL subsystem! %s\n", SDL_GetError());
        return false;
    }

    //char* appName = "CHIP8 Emulator - ";
    //strcat(appName, rom_name);

    sdl->window = SDL_CreateWindow("CHIP8 Emulator", SDL_WINDOWPOS_CENTERED, 
                                SDL_WINDOWPOS_CENTERED,
                                config.window_width * config.scale_factor,
                                config.window_height * config.scale_factor, 
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

                    default:
                        break;
                }
                break;

            case SDL_KEYUP:
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
                    if (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]){
                        chip8->V[0xF] = 1;
                    }
                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                    break;
                case 6:
                    // 0x8XY6: Set register VX >>= 1, store shifted off bit in VF
                    chip8->V[0xF] = chip8->V[chip8->inst.X] & 1;
                    chip8->V[chip8->inst.X] >>= 1;
                    break;
                case 7:
                    // 0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive)
                    if (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]){
                        chip8->V[0xF] = 1;
                    }
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    break;
                case 0xE:
                    // 0x8XYE: Set register VX <<= 1, store shifted off bit in VF
                    chip8->V[0xF] = (chip8->V[chip8->inst.X] & 0x80 >> 7);
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

        default:
            break;
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
    if(!init_sdl(&sdl, config)) exit(EXIT_FAILURE);

    // Initialize CHIP8 machine
    chip8_t chip8 = {0};
    
    if(!init_chip8(&chip8, rom_name)) exit(EXIT_FAILURE);

    // Initial screen clear
    clear_screen(sdl, config);
    
    // Main emulator loop
    while(chip8.state != QUIT){
        // Handle user input
        handle_input(&chip8);

        if(chip8.state == PAUSED) continue;

        emulate_instructions(&chip8, config);

        // Get_time();
        // Emulate CHIP8 instructions
        // Get_time(); elapsed since last get_time();

        // Delay for 60hz
        SDL_Delay(16);

        update_screen(sdl, config, chip8);
    }

    // Final cleanup
    final_cleanup(sdl);

    exit(EXIT_SUCCESS);
}