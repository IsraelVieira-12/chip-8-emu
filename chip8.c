#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h"

bool init_sdl(void){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER ) != 0) {
        SDL_Log("Could not initialize SDL subsystem! %s\n", SDL_GetError());
        return false;
    }

    return true;
}

void final_cleanup(void){
    SDL_Quit(); // Shut down SDL subsystem
}

// Main function
int main(int argc, char **argv){
    (void)argc;
    (void)argv;

    // Initialize SDL
    if(!init_sdl()) exit(EXIT_FAILURE);

    // Final cleanup
    final_cleanup();

    puts("TESTING CHIP 8");

    exit(EXIT_SUCCESS);
}