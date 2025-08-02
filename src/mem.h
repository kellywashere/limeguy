#ifndef __MEM_H__
#define __MEM_H__

#include <stdbool.h>
#include "common.h"
#include "rom.h"
#include "gameboy.h"

// mem takes care of memory mapping

struct mem {
	struct rom*     rom;
	u8*             ram;
	u8              oam[0xA0];
	u8              io[0x80];
	u8              hiram[0x7F];
	u8              ie; // IE interrupt enbl flags. Note: IF is at io[0x0F]

	// DMA state
	bool            dma_requested; // set when writing to 0xFF46
	bool            dma_next_cycle; // hand-over bool to delay by one cycle
	u16             dma_request_addres;
	bool            dma_active;
	u16             dma_addr;

	bool            div_was_reset; // To allow syncing DIV timer to a write op

	u8              button_state; // keeping copy of this simplifies interrupt gen, e.g.

	//gb_color*   tiles; // For pre-decoded tiles
};

struct mem* mem_create();
void mem_destroy(struct mem* mem);

void mem_connect_rom(struct mem* mem, struct rom* rom);
void mem_disconnect_rom(struct mem* mem);

u8 mem_read(struct mem* mem, u16 addr);
u16 mem_read16(struct mem* mem, u16 addr);

void mem_write(struct mem* mem, u16 addr, u8 value);
void mem_write16(struct mem* mem, u16 addr, u16 value);

u8 mem_get_active_interrupts(struct mem* mem);
void mem_clear_interrupt_flag(struct mem* mem, int nr);

bool mem_is_cpu_double_speed(struct mem* mem);

void mem_mcycle(struct mem* mem); // Only used for DMA

// Timer interace
u8 mem_timers_get_tac(struct mem* mem);
bool mem_timers_sync(struct mem* mem);
void mem_timers_div_inc(struct mem* mem);
void mem_timers_tima_inc(struct mem* mem);

// PPU interface
void mem_ppu_report(struct mem* mem, int ly, int mode);
void mem_ppu_get_scroll(struct mem* mem, u8* scx, u8* scy);
void mem_ppu_get_wxwy(struct mem* mem, u8* wx, u8* wy);
u8 mem_ppu_get_lcdc(struct mem* mem);
int mem_ppu_get_tileidx_from_tilemap(struct mem* mem, int tm_idx);
void mem_ppu_copy_tile_row(struct mem* mem, gb_color_idx* dest, int tile_idx_eff, int tile_row);
void mem_ppu_get_bg_palette(struct mem* mem, gb_color palette[4]);

// Buttons (kept in mem because of interrupt handling)
void mem_set_button(struct mem* mem, enum gb_button but, bool pressed);


#endif
