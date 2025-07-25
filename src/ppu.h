#ifndef __PPU_H__
#define __PPU_H__

enum mode {
	PPU_MODE_HBLANK  = 0,
	PPU_MODE_VBLANK  = 1,
	PPU_MODE_OAMSCAN = 2,
	PPU_MODE_DRAW    = 3
};

struct ppu {
	struct mem* mem;
	int         xdot; // 0 .. 455
	int         ly;   // 0 .. 153
	enum mode   mode;
};

struct ppu* ppu_create(struct mem* mem);
void ppu_destroy(struct ppu* ppu);

void ppu_mcycle(struct ppu* ppu);

#endif

