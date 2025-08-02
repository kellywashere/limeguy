#ifndef __PPU_H__
#define __PPU_H__

#include <stdbool.h>
#include <stdint.h>
#include "common.h"

#define LCD_WIDTH 160
#define LCD_HEIGHT 144

// LCD color code when screen off (5th color)
#define COLOR_LCD_OFF 0x4

enum ppu_mode {
	PPU_MODE_HBLANK  = 0,
	PPU_MODE_VBLANK  = 1,
	PPU_MODE_OAMSCAN = 2,
	PPU_MODE_DRAW    = 3
};

struct ppu {
	struct mem*   mem;
	bool          enabled;
	int           xdot; // 0 .. 455
	int           ly;   // 0 .. 153
	enum ppu_mode mode;

	gb_color      lcd[LCD_WIDTH * LCD_HEIGHT];

	bool          frame_done; // set to true when ly goes back to 0

	// helper
	int           last_line_rendered;

	// window
	bool          wy_condition; // WY == LY
	int           wy_counter;

	// DEBUG
	unsigned int nr_frames;
};

struct obj_attributes {
	u8 y;
	u8 x;
	u8 tile_idx;
	u8 flags; // see: https://gbdev.io/pandocs/OAM.html

	u8 idx_in_oam; // for sorting when x's are equal
};

struct ppu* ppu_create(struct mem* mem);
void ppu_destroy(struct ppu* ppu);

void ppu_mcycle(struct ppu* ppu);

// rgba_palette order: lcd col 0, lcd col 1, lcd col 2, lcd col 3, off color
void ppu_lcd_to_rgba(struct ppu* ppu, u8* pixels, int pixw, int pixh, struct limeguy_color rgba_palette[5]);

bool ppu_frame_is_done(struct ppu* ppu);
void ppu_reset_frame_done(struct ppu* ppu);

#endif

