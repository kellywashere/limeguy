#include <stdlib.h>
#include "ppu.h"
#include "mem.h"

#define XDOT_MAX     456
#define LY_MAX       154
#define XDOT_OAMSCAN 80
#define XDOT_DRAW    252 /* debatable...*/
#define LY_VBLANK    144

struct ppu* ppu_create(struct mem* mem) {
	struct ppu* ppu = malloc(sizeof(struct ppu));
	ppu->mem = mem;
	ppu->xdot = 0;
	ppu->ly = 0;
	ppu->mode = PPU_MODE_OAMSCAN;
	return ppu;
}

void ppu_destroy(struct ppu* ppu) {
	free(ppu);
}

void ppu_mcycle(struct ppu* ppu) {
	// one mcycle --> 4 dots (2 dots if cpu in double speed)
	ppu->xdot += mem_is_cpu_double_speed(ppu->mem) ? 2 : 4;
	if (ppu->xdot >= XDOT_MAX) {
		++ppu->ly;
		if (ppu->ly >= LY_MAX) {
			ppu->ly -= LY_MAX; // TODO: Do something else here?
		}
		ppu->xdot -= XDOT_MAX;
	}
	ppu->mode = ppu->ly >= LY_VBLANK     ? PPU_MODE_VBLANK :
	            ppu->xdot < XDOT_OAMSCAN ? PPU_MODE_OAMSCAN :
	            ppu->xdot < XDOT_DRAW    ? PPU_MODE_DRAW :
	                                       PPU_MODE_HBLANK;
	// update values in mem
	mem_ppu_report(ppu->mem, ppu->ly, (int)ppu->mode);
}
