#ifndef __GAMEBOY_H__
#define __GAMEBOY_H__

#include "rom.h"
#include "mem.h"
#include "timers.h"
#include "mcycle.h"
#include "cpu.h"
#include "ppu.h"

struct gameboy {
	struct rom*    rom;
	struct mem*    mem;
	struct timers* timers;
	struct ppu*    ppu;
	struct mcycle* mcycle;
	struct cpu*    cpu;
};

struct gameboy* gameboy_create(const char* rom_file_name);
void gameboy_destroy(struct gameboy* gameboy);

#endif
