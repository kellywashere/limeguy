#include <stdlib.h>
#include "mcycle.h"
#include "timers.h"
#include "ppu.h"

// TODO: seperate "connect" functions instead of passing all to create?
struct mcycle* mcycle_create(struct timers* timers, struct ppu* ppu) {
	struct mcycle* mcycle = malloc(sizeof(struct mcycle));
	mcycle->timers = timers;
	mcycle->ppu = ppu;
	return mcycle;
}

void mcycle_destroy(struct mcycle* mcycle) {
	free(mcycle);
}

void mcycle_tick(struct mcycle* mcycle) {
	if (mcycle->timers)
		timers_mcycle(mcycle->timers);
	if (mcycle->ppu)
		ppu_mcycle(mcycle->ppu);
}

