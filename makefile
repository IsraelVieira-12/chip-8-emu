CFLAGS=-std=c17 -Wall -Wextra -Werror
SDLCONFIG=`sdl2-config --cflags --libs`

all:
	gcc chip8.c -o chip8.exe $(CFLAGS) $(SDLCONFIG)