#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
//#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h> //#include "SDL.h"

//Initalize SDL
bool init_sdl(void)
{
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
    {
        SDL_Log("Could not initialize SDL subsystems! %s\n", SDL_GetError());
        return false;
    }
    return true; //Success
}

// Final cleanup
void final_cleanup(void)
{
    SDL_Quit(); //Shut down SDL subsystems
}

//a squeeze do mr. gui
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    //initialize SDL
    if(!init_sdl()) exit(EXIT_FAILURE);

    // Final cleanup
    final_cleanup();
    //puts("Testando no GNU/Linux");

    exit(EXIT_SUCCESS);
}