#include <stdlib.h>
#include <stdio.h>
#include "rom.h"

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
	return rom;
}

void rom_destroy(struct rom* rom) {
	if (rom) {
		free(rom->data);
		free(rom);
	}
}

