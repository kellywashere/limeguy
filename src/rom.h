#ifndef __ROM_H__
#define __ROM_H__

#include <stddef.h>
#include "typedefs.h"

struct rom {
	i8*          data;
	unsigned int size;
	unsigned int bank;
};

struct rom* rom_create(const char* filename);
void rom_destroy(struct rom* rom);

u16 rom_get_type(struct rom* rom);

i8 rom_read(struct rom* rom, u16 addr);
void rom_write(struct rom* rom, u16 addr, i8 value);

#endif

