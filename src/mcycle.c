#include <stdlib.h>
#include "mcycle.h"
#include "timers.h"

struct mcycle* mcycle_create(struct timers* timers) {
	struct mcycle* mcycle = malloc(sizeof(struct mcycle));
	mcycle->timers = timers;
	return mcycle;
}

void mcycle_destroy(struct mcycle* mcycle) {
	free(mcycle);
}

void mcycle_tick(struct mcycle* mcycle) {
	timers_mcycle(mcycle->timers);
}

