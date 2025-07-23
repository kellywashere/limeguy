#include <stdlib.h>
#include <stdio.h>
#include "rom.h"

#define CARTTYPE_ADDR 0x0147

static
i8* read_binary_file(const char* fname, unsigned int* size) {
	FILE *f = fopen(fname, "rb");
	if (!f) {
		fprintf(stderr, "Error loading ROM file %s", fname);
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	i8* data = malloc(*size);
	fread(data, 1, *size, f);
	fclose(f);
	return data;
}

struct rom* rom_create(const char* filename) {
	struct rom* rom = malloc(sizeof(struct rom));
	rom->size = 0;
	rom->data = read_binary_file(filename, &rom->size);
	//printf("Loaded %s: size $%04X (%u)\n", filename, rom->size, rom->size);
	rom->bank = 1;
	return rom;
}

void rom_destroy(struct rom* rom) {
	if (rom) {
		free(rom->data);
		free(rom);
	}
}

u16 rom_get_type(struct rom* rom) {
	return rom->data[CARTTYPE_ADDR] & 0x0FF;
}

i8 rom_read(struct rom* rom, u16 addr) {
	unsigned int addr_eff = addr;
	addr_eff = addr_eff >= 0x4000 ? (addr & 0x3FFF) + rom->bank * 0x4000 : addr_eff;
	return rom->data[addr_eff];
}

void rom_write(struct rom* rom, u16 addr, i8 value) {
	addr >>= 13;
	switch (addr) {
		case 0: // RAM enbl
			break; // Nothing implemented
		case 1: // ROM bank nr
			rom->bank = value & 0x1F;
			rom->bank = rom->bank == 0 ? 1 : rom->bank;
			//printf("ROM: selected bank %u\n", rom->bank);
			break;
		case 2: // RAM bank nr or upper 2 bits of bank nr
			printf("ROM: write to 0x4000 - 0x5FFF not implemented yet\n");
			break;
		case 3: // Banking mode
			printf("ROM: write to 0x6000 - 0x7FFF not implemented yet\n");
			break;
	}
}
