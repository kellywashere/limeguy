// TODO: VRAM access depending on PPU state
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "mem.h"

#define VRAM          0x8000
#define TILEDATA      0x8000
#define TILEMAP       0x9800

#define ECHO_RAM      0xE000
#define ECHO_RAM_SIZE 0x1E00
#define ECHO_RAM_OFFS 0x2000

// OAM
#define OAM_START 0xFE00
#define OAM_SIZE  0x00A0

// IO
#define IO_START  0xFF00
#define IO_SIZE   0x0080

// Next defines are offsets to 0xFF00
#define IO_SB   0x01 /* Serial data */
#define IO_SC   0x02 /* Serial control */
#define IO_DIV  0x04 /* Timer DIV */
#define IO_TIMA 0x05 /* Timer counter */
#define IO_TMA  0x06 /* Timer modulo */
#define IO_TAC  0x07 /* Timer control */
#define IO_IF   0x0F /* interrupt flag */
#define IO_LCDC 0x40 /* LCD Control */
#define IO_STAT 0x41 /* LCD status */
#define IO_SCY  0x42 /* Y scroll */
#define IO_SCX  0x43 /* X scroll */
#define IO_LY   0x44 /* LCD LY */
#define IO_LYC  0x45 /* LY cmp */
#define IO_BGP  0x47 /* BG Palette */

#define HIRAM_START   0xFF80
#define HIRAM_SIZE    0x7F

#define INTERRUPT_ENABLE 0xFFFF

#define INTR_VBLANK 0
#define INTR_LCD    1
#define INTR_TIMER  2
#define INTR_SERIAL 3
#define INTR_JOYPAD 4

#define LY_VBLANK   144 /* needed here for vbalnk interrupt */

// DEBUG
#define IO_UNUSED 0x03 /* if non-zero: breakpoint */

struct io_init {
	u16 offset;
	u16 val;
};

#include "io_init.inc" // defines table io_init_dmg0[] 

#define NR_TILES          384

#define RAM_RESERVED      (32*1024)
//Next line is for when tiles are pre-computed, see note at mem_ppu_copy_tile_row
//#define TILEDATA_RESERVED (NR_TILES * 8 * 8)

struct mem* mem_create() {
	// reverve one piece of mem for all (avoid many mallocs)
	struct mem* mem = malloc(sizeof(struct mem) + RAM_RESERVED/* + TILEDATA_RESERVED */);
	mem->rom = NULL;
	mem->ram = (u8*)((void*)mem + sizeof(struct mem)); // ram follows struct directly
	//mem->tiles = (u8*)((void*)mem->ram + RAM_RESERVED); // for "pre-decoded" tiles

	// Init IO vals
	// 0xFF4D needs to return FF for cpu_instrs.gb to pass
	for (int ii = 0; ii < IO_SIZE; ++ii)
		mem->io[ii] = 0xFF; // see: https://www.reddit.com/r/EmuDev/comments/ipap0w/blarggs_cpu_tests_and_the_stop_instruction/
	for (unsigned int ii = 0; ii < sizeof(io_init_dmg0) / sizeof(io_init_dmg0[0]); ++ii)
		mem->io[io_init_dmg0[ii].offset] = io_init_dmg0[ii].val;
	mem->ie = 0;

	// DEBUG  TODO: Remove
	mem->io[IO_UNUSED] = 0;

	return mem;
}

void mem_destroy(struct mem* mem) {
	free(mem);
}

void mem_connect_rom(struct mem* mem, struct rom* rom) {
	mem->rom = rom;
}

void mem_disconnect_rom(struct mem* mem) {
	mem->rom = NULL;
}

u8 mem_read(struct mem* mem, u16 addr) {
	if (addr >= ECHO_RAM && addr < ECHO_RAM + ECHO_RAM_SIZE)
		addr -= ECHO_RAM_OFFS;
	if (addr < VRAM) // ROM bank 00 & 01
		return rom_read(mem->rom, addr);
	else if (addr >= VRAM && addr < ECHO_RAM)
		return mem->ram[addr - VRAM]; // includes echo RAM
	else if (addr >= HIRAM_START && addr < (HIRAM_START + HIRAM_SIZE))
		return mem->hiram[addr - HIRAM_START];
	else if (addr >= OAM_START && addr < (OAM_START + OAM_SIZE)) {
		return mem->oam[addr & 0xFF]; // TODO
	}
	else if (addr >= IO_START && addr < (IO_START + IO_SIZE)) { // IO operation
		u8 io_idx = addr & 0xFF;
	
		// FIXME: this is a hack to pass gb doctor
		//if (io_idx == IO_LY) return 0x90;

		return mem->io[io_idx]; // TODO
	}
	else if (addr == INTERRUPT_ENABLE)
		return mem->ie;
	else
		printf("Unhandled address read: addr = $%04X\n", addr);
	return 0x66;
}

u16 mem_read16(struct mem* mem, u16 addr) {
	u8 lsbyte = mem_read(mem, addr++);
	u8 msbyte = mem_read(mem, addr);
	return (((u16)msbyte) << 8) | (u16)lsbyte;
}

void mem_write(struct mem* mem, u16 addr, u8 value) {
	if (addr >= ECHO_RAM && addr < ECHO_RAM + ECHO_RAM_SIZE)
		addr -= ECHO_RAM_OFFS;
	if (addr < VRAM) // ROM bank 00 & 01
		rom_write(mem->rom, addr, value);
	else if (addr >= VRAM && addr < ECHO_RAM)
		mem->ram[addr - VRAM] = value; // includes echo RAM
	else if (addr >= HIRAM_START && addr < (HIRAM_START + HIRAM_SIZE))
		mem->hiram[addr - HIRAM_START] = value;
	else if (addr >= OAM_START && addr < (OAM_START + OAM_SIZE)) {
		mem->oam[addr & 0xFF] = value;
	}
	else if (addr >= IO_START && addr < (IO_START + IO_SIZE)) { // IO operation
		u8 io_idx = addr & 0xFF;
		switch (io_idx) {
			case IO_LY: // read only
				break;
			case IO_STAT:
				mem->io[io_idx] = value & 0xF8; // 3 lsb are read only
				break;
			case IO_SC: // Serial out
				if (value == 0x81) {
					printf("%c", mem->io[IO_SB]);
					mem->io[io_idx] = 0;
				}
				break;
			case IO_DIV:
				mem->io[io_idx] = 0;
				break;
			default:
				mem->io[io_idx] = value;
		}
	}
	else if (addr == INTERRUPT_ENABLE)
		mem->ie = value;
	else
		printf("Unhandled address write: addr = $%04X\n", addr);
}

void mem_write16(struct mem* mem, u16 addr, u16 value) {
	mem_write(mem, addr++, value & 0x00FF);
	mem_write(mem, addr, value >> 8);
}

u8 mem_get_active_interrupts(struct mem* mem) {
	return mem->ie & mem->io[IO_IF];
}

void mem_set_interrupt_flag(struct mem* mem, int nr) {
	mem->io[IO_IF] |= (1 << nr);
}

void mem_clear_interrupt_flag(struct mem* mem, int nr) {
	mem->io[IO_IF] &= ~(1 << nr);
}

bool mem_is_cpu_double_speed(struct mem* mem) {
	(void)mem;
	// TODO: return bit 7 of KEY1 ?
	return false;
}

u8 mem_timers_get_tac(struct mem* mem) {
	return mem->io[IO_TAC] & 0x07;
}


void mem_timers_div_inc(struct mem* mem) {
	// TODO: Putting this fn in mem in stead of timers is hacky. Move to timers !
	++mem->io[IO_DIV];
}

void mem_timers_tima_inc(struct mem* mem) {
	// TODO: Putting this fn in mem in stead of timers is hacky. Move to timers !
	u16 newtima = (u16)mem->io[IO_TIMA] + 1;
	if (newtima == 256) { // overflow
		mem->io[IO_TIMA] = mem->io[IO_TMA];
		mem_set_interrupt_flag(mem, INTR_TIMER);
	}
	else
		mem->io[IO_TIMA] = newtima & 0x0FF;
}


void mem_ppu_report(struct mem* mem, int ly, int mode){
	// TODO: Spurious STAT interrupt: https://gbdev.io/pandocs/STAT.html#spurious-stat-interrupts

	// TODO: Putting this fn in mem in stead of PPU is hacky. Move to PPU !
	u8 stat_prev = mem->io[IO_STAT];
	int mode_prev = stat_prev & 0x3;
	int ly_prev = mem->io[IO_LY];

	// previous output of interrupt OR
	bool mode_match_prev = mode_prev != 3 && (stat_prev & (1 << (mode_prev + 3))) != 0; // modexintsel & modebit
	bool lyc_match_prev = ((stat_prev >> 2) & (stat_prev >> 6) & 1) != 0;  //LYCintsel & LYC==LY
	bool or_prev = mode_match_prev || lyc_match_prev;

	mem->io[IO_LY] = ly;

	u8 stat = (stat_prev & 0xF8) | (ly == mem->io[IO_LYC] ? 4 : 0) | mode;
	mem->io[IO_STAT] = stat;
	
	// new output of interrupt OR
	bool mode_match = mode != 3 && (stat & (1 << (mode + 3))) != 0; // modexintsel & modebit
	bool lyc_match = ((stat >> 2) & (stat >> 6) & 1) != 0;
	bool or = mode_match || lyc_match;
	
	if (ly_prev < LY_VBLANK && ly >= LY_VBLANK)
		mem_set_interrupt_flag(mem, INTR_VBLANK);
	if (!or_prev && or)
		mem_set_interrupt_flag(mem, INTR_LCD);
}

u8 mem_ppu_get_lcdc(struct mem* mem) {
	return mem->io[IO_LCDC];
}

void mem_ppu_get_scroll(struct mem* mem, u8* scx, u8* scy) {
	*scx = mem->io[IO_SCX];
	*scy = mem->io[IO_SCY];
}

int mem_ppu_get_tileidx_from_tilemap(struct mem* mem, int tm_idx) {
	// tm_idx already contains the offset created by LCDC.3
	return mem->ram[(TILEMAP - VRAM) + tm_idx];
}

// TODO: Try possible speed-up:
// Draw all tiles in mem (array of tiles).
// Update tile when VRAM tile data changes.
// Copy from there directly.
void mem_ppu_copy_tile_row(struct mem* mem, gb_color_idx* dest, int tile_idx_eff, int tile_row) {
	// tile_idx_eff: 0 .. 383 (LCDC.5 already processed)
	// tile_row: 0 .. 7
	u16 vram_addr = tile_idx_eff * 16 + tile_row * 2;
	u8 row_lsb = mem->ram[vram_addr];
	u8 row_msb = mem->ram[vram_addr + 1];
	for (int b = 0; b < 8; ++b) {
		gb_color_idx col_idx = ((row_msb & 1) << 1) | (row_lsb & 1);
		dest[7 - b] = col_idx;
		row_msb >>= 1;
		row_lsb >>= 1;
	}
}

void mem_ppu_get_bg_palette(struct mem* mem, gb_color palette[4]) {
	int bgp = mem->io[IO_BGP];
	for (int ii = 0; ii < 4; ++ii) {
		palette[ii] = bgp & 0x3;
		bgp >>= 2;
	}
}
