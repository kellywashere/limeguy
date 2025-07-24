#include <stdbool.h>
#include <stdlib.h>
#include "timers.h"
#include "mem.h"

#define DIV 0xFF04
#define TAC 0xFF07

#define DIVCOUNT 64 /* 2^20 Hz / 16384 Hz */


struct timers* timers_create(struct mem* mem) {
	struct timers* timers = malloc(sizeof(struct timers));
	timers->count_div = 0;
	timers->count_tima = 0;
	timers->mem = mem;
	return timers;
}

void timers_destroy(struct timers* timers) {
	free(timers);
}

void timers_mcycle(struct timers* timers) { // called every M-cycle = 4 T-cycles
	int clockselect_to_maxcount[4] = { 256, 4, 16, 64 };
	
	// DIV
	++timers->count_div;
	if (timers->count_div >= DIVCOUNT) {
		timers->count_div = 0;
		mem_divtimer_inc(timers->mem);
	}

	i8 tac = mem_read(timers->mem, TAC) & 0x07;
	// i8 tac = timers->mem->io[TAC & 0x0FF] & 0x03; /* FASTER... TODO: SPEED UP mem interactions */
	if (tac >> 2) { // enbl
		++timers->count_tima;
		if (timers->count_tima >= clockselect_to_maxcount[tac & 0x03]) {
			timers->count_tima = 0;
			mem_tima_inc(timers->mem); // also takes care of overflow + interrupt flags
		}
	}
}


