CFLAGS=-std=c17 -Wall -Wextra -Werror

ifeq ($(OS),Windows_NT)
	OUTPUT=chip8.exe
	CFLAGS += -Wl,-subsystem,console
else
	OUTPUT=chip8.out
endif

#CONFIG=`sdl2-config --cflags --libs`
CONFIG=$(shell pkg-config --cflags --libs sdl2)

all:
	gcc chip8.c -o $(OUTPUT) $(CFLAGS) $(CONFIG)

debug:
	gcc chip8.c -o $(OUTPUT) $(CFLAGS) $(CONFIG) -DDEBUG