// Interesting links:
// https://jsgroth.dev/blog/posts/gb-rewrite-pixel-fifo/

#include <stdlib.h>
#include "ppu.h"
#include "mem.h"

//DEBUG
#include <stdio.h>

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
		mem_ppu_copy_tile_row(mem, &full_line[tilex * 8], tile_idx_eff, tile_y, false);
	}
}


static
int cmp_obj_xpos_rev(const void* a, const void* b) {
	struct obj_attributes* oa_a = (struct obj_attributes*)a;
	struct obj_attributes* oa_b = (struct obj_attributes*)b;
	return (oa_a->x != oa_b->x) ? oa_b->x - oa_a->x : oa_b->idx_in_oam - oa_a->idx_in_oam;
}

static
void ppu_draw_obj_line(gb_color_idx obj_line[], u8 obj_flags[], int y, struct mem* mem, int obj_height) {
	int nr_objs = 0; // nr of objects on this line
	struct obj_attributes obj_attribs[10]; // we can draw 10 objs on one line
	gb_color_idx obj_tile_line[8];  // temporary space for tile line

	int y16 = y + 16; // we have 16 px margin on top, for hiding parts of objs

	// step 0: clear obj line
	for (int ii = 0; ii < LCD_WIDTH; ++ii)
		obj_line[ii] = 0; // transparent

	// Step 1: get first 10 objects that have y-pos on this line
	for (int ii = 0; ii < 40 && nr_objs < 10; ++ii) {
		mem_ppu_get_obj_attribs(mem, &obj_attribs[nr_objs], ii);
		// on this line?
		if (obj_attribs[nr_objs].y <= y16 && y16 < obj_attribs[nr_objs].y + obj_height) {
			// it's a keeper
			if (obj_height == 16)
				obj_attribs[nr_objs].idx_in_oam &= 0xFE; // correct idx when using tile height 16
			++nr_objs;
		}
	}

	// Step 2: draw objects, from highest x-coord down (so lowest x-pos overdraws previous one,
	// for correct drawing priority). We save objects prio bit in prio array
	// Step 2a: sort, descending x pos
	qsort(obj_attribs, nr_objs, sizeof(struct obj_attributes), cmp_obj_xpos_rev);
	// Step 2b: draw each obj on obj_line, saving flags when drawing a pixel
	for (int ii = 0; ii < nr_objs; ++ii) {
		u8 flags = obj_attribs[ii].flags;
		// get the tile row
		bool fliplr = ((flags >> 5) & 1) == 1;
		bool flipud = ((flags >> 6) & 1) == 1;
		int y_in_tile = flipud ? obj_height - 1 - (y16 - obj_attribs[ii].y) : y16 - obj_attribs[ii].y;
		int tile_idx = obj_attribs[ii].tile_idx;
		if (y_in_tile >= 8) {
			y_in_tile -= 8;
			++tile_idx;
		}
		mem_ppu_copy_tile_row(mem, obj_tile_line, tile_idx, y_in_tile, fliplr);
		// copy to obj line
		for (int x = 0; x < 8; ++x) {
			u8 xtot = obj_attribs[ii].x + x;
			if (xtot < 8 || xtot >= (LCD_WIDTH + 8) || obj_tile_line[x] == 0) continue; // not visible
			xtot -= 8;
			obj_line[xtot] = obj_tile_line[x];
			obj_flags[xtot] = obj_attribs[ii].flags;
		}
	}
}

static
void ppu_draw_scanline(struct ppu* ppu) {
	u8 scx, scy, wx, lcdc;
	// TODO: Merge into single scanline for speed (low prio for now)
	gb_color_idx bg_line[32 * 8];
	gb_color_idx win_line[32 * 8]; // TODO: 21 tiles is enough
	// obj line, and corresponding flags
	gb_color_idx obj_line[LCD_WIDTH];
	u8           obj_flags[LCD_WIDTH];

	// get positions of backround and window
	mem_ppu_get_scroll(ppu->mem, &scx, &scy);
	mem_ppu_get_wxwy(ppu->mem, &wx, NULL); // we don't need wy, handled by wy_condition

	// extract data from LCDC
	lcdc = mem_ppu_get_lcdc(ppu->mem);
	bool addrmode8000 = ((lcdc >> 4) & 1) == 1; // tile data addressing mode bg/win
	int bg_tile_map = (lcdc >> 3) & 1;  // tile map 0 or 1
	int win_tile_map = (lcdc >> 6) & 1; // tile map 0 or 1
	int obj_height = 8 + ((lcdc >> 2) & 1) * 8;
	bool obj_enbl = (lcdc >> 1) & 1;
	bool bgwin_enbl = (lcdc >> 0) & 1;
	// win_enbl: is window visible in this scanline?
	bool win_enbl = bgwin_enbl && ppu->wy_condition && wx <= 166 && ((lcdc >> 5) & 1) == 1;

	// Background and window
	if (bgwin_enbl) { // background
		int y_eff = (ppu->ly + scy) & 0xFF;
		ppu_draw_full_line_of_tilemap(bg_line, y_eff, ppu->mem, bg_tile_map, addrmode8000);
	}
	if (win_enbl) { // window
		// TODO: no need to get entire 32 tile line, max is 7px + 160px, so 21 tiles is enough
		ppu_draw_full_line_of_tilemap(win_line, ppu->wy_counter, ppu->mem, win_tile_map, addrmode8000);
		++ppu->wy_counter;
	}
	if (obj_enbl) { // objects
		ppu_draw_obj_line(obj_line, obj_flags, ppu->ly, ppu->mem, obj_height);
	}

	// Get palette into LUT array
	gb_color bg_palette[4];
	gb_color obj_palettes[2 * 4];
	mem_ppu_get_bg_palette(ppu->mem, bg_palette);
	mem_ppu_get_obj_palettes(ppu->mem, obj_palettes);

	// Multiplex to lcd screen bitmap, and apply palette
	int screen_offset = ppu->ly * LCD_WIDTH;
	for (int x = 0; x < LCD_WIDTH; ++x) {
		int x_bg = (x + scx) & 0xFF;
		bool is_win = win_enbl && x + 7 >= wx;
		gb_color_idx bgwin_col_idx = bgwin_enbl ? (is_win ? win_line[x + 7 - wx] : bg_line[x_bg]) : 0;
		gb_color bgwin_col = bgwin_enbl ? bg_palette[bgwin_col_idx] : 0; // WHITE when !bgwin_enbl
		// BG/WIN vs OBJ
		if (!obj_enbl || obj_line[x] == 0) // No obj here, draw bg/win
			ppu->lcd[screen_offset + x] = bgwin_col;
		else { // there is obj pixel here
			bool prio = ((obj_flags[x] >> 7) & 1) == 1;
			if (prio && bgwin_col_idx != 0) // draw bg/win over obj instead
				ppu->lcd[screen_offset + x] = bgwin_col;
			else { // draw obj pixel
				int palette_nr = (obj_flags[x] >> 4) & 1;
				ppu->lcd[screen_offset + x] = obj_palettes[palette_nr * 4 + obj_line[x]];
			}
		}
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
	if (ppu->mode == PPU_MODE_OAMSCAN && ppu->xdot >= 76 && ppu->ly != ppu->last_line_rendered) {
		// "76" is at end of OAM, giving as much time as possbile for interrupt to be processed
		// TODO: keep record of some IO vars (like LCDC) per pixel!
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

