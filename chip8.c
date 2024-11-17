#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<stdbool.h>

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
} config_t;

// Emulator states
typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

// CHIP8 Machine object
typedef struct {
    emulator_state_t state;
} chip8_t;

// Initialize SDL
bool init_sdl(sdl_t *sdl, const config_t config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("Could not initialize SDL subsystems! %s\n", SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow("Chip8 Emulator", SDL_WINDOWPOS_CENTERED,
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
        .bg_color = 0xFFFF00FF, // YELLOW
        .scale_factor = 20,     // Default resolution will ve 1280x640

    };

    // Override defaults from passed in arguments
    for (int i = 1; i < argc; i++) {
        (void)argv[i]; // Prevent compiler error from unused variables argc/argv
        // ..
    }

    return true; 
}

// Initialize CHIP* machine
bool init_chip8(chip8_t *chip8) {
    chip8->state = RUNNING;
    return true;
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
void update_screen(const sdl_t sdl) {
    SDL_RenderPresent(sdl.renderer);
}

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

//Squeze
int main(int argc, char **argv) {
    // Initialize emulator configuration/options
    config_t config = {0};
    if (!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);

    // Initialize SDL
    sdl_t sdl = {0};
    if (!init_sdl(&sdl, config)) exit(EXIT_FAILURE);

    // initialize chip8 machine
    chip8_t chip8 = {0};
    if (!init_chip8(&chip8)) exit(EXIT_FAILURE);

    // Initial screen clear
    clear_screen(sdl, config);

    // Main emulator loop
    while (chip8.state != QUIT){
        // handle user input
        handle_input(&chip8);

        // if (chip*.state == PAUSED) continue;

        // Get_time();

        //handle_input();
        // Emulate chip8 instructions
        // Get_time() elapsed since last get_time();
        // Delay for 60hz/60fps
        // SDL_Delay(16 - actual time);
        SDL_Delay(16);
        // Update window with changes
        update_screen(sdl);

    }

    // Final cleanup
    final_cleanup(sdl);
    exit(EXIT_SUCCESS);
}