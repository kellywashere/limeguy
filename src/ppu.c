// Interesting links:
// https://jsgroth.dev/blog/posts/gb-rewrite-pixel-fifo/

// BUG: There is one scan line missing in fairylake.gb

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
	//ppu->xdot = 0;
	ppu->xdot = 100; // To pass mooneye boot check
	ppu->ly = LY_MAX - 9; // To pass mooneye boot check
	ppu->mode = PPU_MODE_HBLANK;
	ppu->wy_condition = false;
	ppu->wy_counter = 0;
	ppu->last_line_rendered = -1;
	ppu->enabled = true;
	ppu->frame_done = false;
	ppu->nr_frames = 0;
	return ppu;
}

void ppu_destroy(struct ppu* ppu) {
	free(ppu);
}

static
void ppu_draw_full_line_of_tilemap(gb_color_idx full_line[], int y, struct mem* mem, int tile_map_sel, bool addrmode8000) {
	// tm_idx := tilemap * 32 * 32 + y/8 * 32 + x/8
	int tm_idx_offset = tile_map_sel * 32 * 32 + (y / 8) * 32;
	int tile_y = y % 8; // y in tile

	// Create entire 32 tile BG scanline
	for (int tilex = 0; tilex < 32; ++tilex) {
		int tile_idx = mem_ppu_get_tileidx_from_tilemap(mem, tm_idx_offset + tilex); // as in tilemap
		int tile_idx_eff = addrmode8000 ?  // oonverted to 0..383
		                   tile_idx :
		                   256 + (tile_idx & 0x7F) - (tile_idx & 0x80);
		mem_ppu_copy_tile_row(mem, &full_line[tilex * 8], tile_idx_eff, tile_y);
	}
}

static
void ppu_draw_scanline(struct ppu* ppu) {
	u8 scx, scy, wx, lcdc;
	mem_ppu_get_scroll(ppu->mem, &scx, &scy);
	mem_ppu_get_wxwy(ppu->mem, &wx, NULL); // we don't need wy, handled by wy_condition
	lcdc = mem_ppu_get_lcdc(ppu->mem);
	int bg_tile_map = (lcdc >> 3) & 1;  // tile map 0 or 1
	int win_tile_map = (lcdc >> 6) & 1; // tile map 0 or 1
	bool bg_enbl = (lcdc >> 0) & 1;
	// win_enbl: is window visible in this scanline?
	bool win_enbl = bg_enbl && ppu->wy_condition && wx <= 166 && ((lcdc >> 5) & 1) == 1;
	bool addrmode8000 = ((lcdc >> 4) & 1) == 1; // tile data addressing mode

	// TODO: Merge into single scanline for speed (low prio for now)
	gb_color_idx full_line_bg[32 * 8];
	gb_color_idx full_line_win[32 * 8];

	// BACKGROUND
	if (bg_enbl) {
		int y_eff = (ppu->ly + scy) & 0xFF;
		ppu_draw_full_line_of_tilemap(full_line_bg, y_eff, ppu->mem, bg_tile_map, addrmode8000);
	}
	if (win_enbl) {
		ppu_draw_full_line_of_tilemap(full_line_win, ppu->wy_counter, ppu->mem, win_tile_map, addrmode8000);
		++ppu->wy_counter;
	}

	// Get palette into LUT array
	gb_color bg_palette[4];
	mem_ppu_get_bg_palette(ppu->mem, bg_palette);

	// Copy to lcd screen bitmap, after applying palette
	int screen_offset = ppu->ly * LCD_WIDTH;
	for (int x = 0; x < LCD_WIDTH; ++x) {
		int x_bg = (x + scx) & 0xFF;
		bool is_win = win_enbl && x + 7 >= wx;
		gb_color_idx pix_col_idx = is_win ? full_line_win[x + 7 - wx] : full_line_bg[x_bg];
		ppu->lcd[screen_offset + x] = bg_palette[pix_col_idx];
	}

	ppu->last_line_rendered = ppu->ly;
}

void ppu_mcycle(struct ppu* ppu) {
	// called every CPU cycle.
	// Takes care of updating xdot and ly, setting mode, calling scan line draw
	bool enbl = (mem_ppu_get_lcdc(ppu->mem) >> 7) == 1;
	if (ppu->enabled && !enbl) { // LCD just turned off: make screen whiter-than-white
		for (int ii = 0; ii < LCD_WIDTH * LCD_HEIGHT; ++ii)
			ppu->lcd[ii] = COLOR_LCD_OFF;
		ppu->mode = PPU_MODE_HBLANK;
		return;
	}
	// one mcycle --> 4 dots (2 dots if cpu in double speed)
	ppu->xdot += mem_is_cpu_double_speed(ppu->mem) ? 2 : 4;

	if (ppu->xdot >= XDOT_MAX) {
		++ppu->ly;
		if (ppu->ly >= LY_MAX) {
			ppu->ly -= LY_MAX;
			ppu->frame_done = true;
			++ppu->nr_frames;
		}
		ppu->xdot -= XDOT_MAX;
	}

	enum ppu_mode mode_prev = ppu->mode;
	ppu->mode = ppu->ly >= LY_VBLANK     ? PPU_MODE_VBLANK :
	            ppu->xdot < XDOT_OAMSCAN ? PPU_MODE_OAMSCAN :
	            ppu->xdot < XDOT_DRAW    ? PPU_MODE_DRAW :
	                                       PPU_MODE_HBLANK;

	if (ppu->mode != mode_prev) {
		if (ppu->mode == PPU_MODE_VBLANK) {
			ppu->wy_condition = false;
			ppu->wy_counter = 0;
		}
		else if (ppu->mode == PPU_MODE_OAMSCAN) { // check window condition
			u8 wy;
			mem_ppu_get_wxwy(ppu->mem, NULL, &wy);
			ppu->wy_condition = ppu->wy_condition || wy == ppu->ly;
		}
	}

	// Update STAT and LY, set interrupt flags
	mem_ppu_report(ppu->mem, ppu->ly, (int)ppu->mode);

	// draw whole scanline during OAMSCAN (not like real hardware...)
	if (ppu->mode == PPU_MODE_OAMSCAN && ppu->xdot >= 40 && ppu->ly != ppu->last_line_rendered) {
		// "40" is arbitrary, just not right at beginning to allow interrupt to be called first
		ppu_draw_scanline(ppu);
	}
}

void ppu_lcd_to_rgba(struct ppu* ppu, u8* pixels, int pixw, int pixh, struct limeguy_color rgba_palette[5]) {
	int w = pixw < LCD_WIDTH ? pixw : LCD_WIDTH;
	int h = pixh < LCD_HEIGHT ? pixh : LCD_HEIGHT;
	for (int y = 0; y < h; ++y) {
		int lcd_idx = y * LCD_WIDTH; // x = 0;
		int pix_idx = 4 * (y * pixw); // 4 bytes per pixel, order: R G B A; x = 0
		for (int x = 0; x < w; ++x) {
			gb_color lcd_col = ppu->lcd[lcd_idx++];
			pixels[pix_idx++] = rgba_palette[lcd_col].r;
			pixels[pix_idx++] = rgba_palette[lcd_col].g;
			pixels[pix_idx++] = rgba_palette[lcd_col].b;
			pixels[pix_idx++] = rgba_palette[lcd_col].a;
		}
	}
}

bool ppu_frame_is_done(struct ppu* ppu) {
	return ppu->frame_done;
}

void ppu_reset_frame_done(struct ppu* ppu) {
	ppu->frame_done = false;
}

