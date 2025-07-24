#ifndef __TIMERS_H__
#define __TIMERS_H__

#include "mem.h"

struct timers {
	struct mem* mem;

	int count_div; // counter for DIV
	int count_tima; // counter for TIMA
};

struct timers* timers_create(struct mem* mem);
void timers_destroy(struct timers* timers);

void timers_mcycle(struct timers* timers);

#endif

