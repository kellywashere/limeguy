#ifndef __ROM_H__
#define __ROM_H__

#include <stddef.h>
#include "typedefs.h"

struct rom {
	i8*          data;
	unsigned int size;
};

struct rom* rom_create(const char* filename);
void rom_destroy(struct rom* rom);

#endif

