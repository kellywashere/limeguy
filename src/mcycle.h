#ifndef __MCYCLE_H__
#define __MCYCLE_H__

// mcycle passes along m-cycle to peripherals that need to be synced

// CPU --> MCYCLE --> PERIPHERALS

struct mcycle {
	struct timers* timers;
	struct ppu*    ppu;
};

struct mcycle* mcycle_create(struct timers* timers, struct ppu* ppu);
void mcycle_destroy(struct mcycle* mcycle);

void mcycle_tick(struct mcycle* mcycle);

#endif
