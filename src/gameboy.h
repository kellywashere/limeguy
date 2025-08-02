#ifndef __GAMEBOY_H__
#define __GAMEBOY_H__

/* Avoid circular header files. We do not need these includes here
#include "rom.h"
#include "timers.h"
#include "mcycle.h"
#include "cpu.h"
#include "ppu.h"
*/

#include <stdbool.h>
#include "common.h"

struct gameboy {
	struct rom*    rom;
	struct mem*    mem;
	struct timers* timers;
	struct ppu*    ppu;
	struct mcycle* mcycle;
	struct cpu*    cpu;

	u8             button_state; // as opposed to GB, a 1-bit means pressed
};

enum gb_button { // correspond to bit nrs in button_state
	BUT_RIGHT = 0,
	BUT_LEFT,
	BUT_UP,
	BUT_DOWN,
	BUT_A,
	BUT_B,
	BUT_SELECT,
	BUT_START
};

struct gameboy* gameboy_create(const char* rom_file_name);
void gameboy_destroy(struct gameboy* gameboy);

/*
u8 gameboy_get_ssba_buttons(struct gameboy* gameboy);
u8 gameboy_get_dpad_buttons(struct gameboy* gameboy);
*/
void gameboy_set_button(struct gameboy* gameboy, enum gb_button but, bool pressed);

#endif
