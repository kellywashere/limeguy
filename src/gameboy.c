#include <stdlib.h>
#include <stdio.h>
#include "gameboy.h"
#include "cpu.h"
#include "mcycle.h"
#include "timers.h"
#include "mem.h"
#include "ppu.h"

struct gameboy* gameboy_create(const char* rom_file_name) {
	// TODO: separate ROM insertion
	struct gameboy* gameboy = malloc(sizeof(struct gameboy));
	gameboy->button_state = 0;

	gameboy->rom = rom_create(rom_file_name);
	if (!gameboy->rom || !gameboy->rom->data)
		return NULL;
	printf("Loaded ROM %s. Type: %02X\n", rom_file_name, rom_get_type(gameboy->rom));

	gameboy->mem = mem_create();
	mem_connect_rom(gameboy->mem, gameboy->rom);

	gameboy->timers = timers_create(gameboy->mem);
	gameboy->timers->count_div = 11; // pass mooneye boot_div-dmg0.gb

	gameboy->ppu = ppu_create(gameboy->mem);
	//gameboy->ppu = NULL;

	struct mcycle* mcycle = mcycle_create(gameboy->timers, gameboy->ppu, gameboy->mem);

	gameboy->cpu = cpu_create(gameboy->mem, mcycle);
	cpu_initregs_dmg0(gameboy->cpu);
	
	return gameboy;
}

void gameboy_destroy(struct gameboy* gameboy) {
	if (gameboy) {
		rom_destroy(gameboy->rom);
		mem_destroy(gameboy->mem);
		timers_destroy(gameboy->timers);
		ppu_destroy(gameboy->ppu);
		mcycle_destroy(gameboy->mcycle);
		cpu_destroy(gameboy->cpu);
		free(gameboy);
	}
}

void gameboy_set_button(struct gameboy* gameboy, enum gb_button but, bool pressed) {
	mem_set_button(gameboy->mem, but, pressed);
}

