#CFLAGS=-std=c17 -Wall -Wextra -Werror
CFLAGS=-std=c17 -Wall -Wl,-subsystem,console -Wextra -Werror

#CONFIG=`sdl2-config --cflags --libs`
CONFIG=`pkg-config --cflags --libs sdl2`

all:
	gcc chip8.c -o chip8.exe $(CFLAGS) $(CONFIG)