#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include "rom.h"
#include "mem.h"
#include "cpu.h"
#include "timers.h"

#define EXTRA_LOGGING

// usage parameters
int break_instrnr = 0; // see usage
int break_addr = -1;
int max_instr = 0;
int start_logging_instrnr = 0;

void print_state_gbdoctor(struct cpu* cpu, FILE* logfile) {
	if (!logfile) return;
	u16 f = (cpu->flags.Z << 7) | (cpu->flags.N << 6) | (cpu->flags.H << 5) | (cpu->flags.C << 4);
	fprintf(logfile, "A:%02X ", (u16)cpu->regs[REG_A] & 0x0FF);
	fprintf(logfile, "F:%02X ", f);
	fprintf(logfile, "B:%02X ", (u16)cpu->regs[REG_B] & 0x0FF);
	fprintf(logfile, "C:%02X ", (u16)cpu->regs[REG_C] & 0x0FF);
	fprintf(logfile, "D:%02X ", (u16)cpu->regs[REG_D] & 0x0FF);
	fprintf(logfile, "E:%02X ", (u16)cpu->regs[REG_E] & 0x0FF);
	fprintf(logfile, "H:%02X ", (u16)cpu->regs[REG_H] & 0x0FF);
	fprintf(logfile, "L:%02X ", (u16)cpu->regs[REG_L] & 0x0FF);
	fprintf(logfile, "SP:%04X ", (u16)cpu->SP);
	fprintf(logfile, "PC:%04X ", (u16)cpu->PC);
	fprintf(logfile, "PCMEM:");
	for (int offs = 0; offs < 4; ++offs)
		fprintf(logfile, "%02X%c", mem_read(cpu->mem, cpu->PC + offs) & 0x0FF,
#ifndef EXTRA_LOGGING
				offs < 3 ? ',':'\n');
#else
				offs < 3 ? ',':' ');
	fprintf(logfile, " ");
	fprintf(logfile, "%c%c%c%c  ", cpu->flags.Z?'Z':'-', cpu->flags.N?'N':'-', cpu->flags.H?'H':'-', cpu->flags.C?'C':'-');
	cpu_fprint_instr_at_pc(cpu, logfile);
#endif
}

int main(int argc, char* argv[]) {
	FILE* logfile = NULL;

	if (argc <= 1) {
		printf("Usage: %s [[b]addr_hex] [[i]instr#] [lLogfile] [sinstr#] [m????] <romfile>\n", argv[0]);
		printf("  b option: breakpoint at address (default: $0100)\n");
		printf("  i option: breakpoint at from instr# (default: 1)\n");
		printf("  l option: game boy doctor output enable (status line after eaach instr)\n");
		printf("  s option: start logging from instr$ given\n");
		printf("  m option: max #instructions to run\n");
		printf("---\n");
		printf("When in step-by-step mode:\n");
		printf("  q: exit program\n");
		printf("  c: continue (exit step-by-step mode)\n");
		printf("  dAddr_hex: display data at address addr_hex\n");
		printf("  lLogfile: start logging a la game boy doctor\n");
		return 1;
	}

	struct mem* mem = mem_create();

	// Parse args
	for (int ii = 1; ii < argc - 1; ++ii) {
		if (argv[ii][0] == 'i') {
			break_instrnr = 1;
			if (isdigit(argv[ii][1]))
				break_instrnr = atoi(argv[ii] + 1);
		}
		else if (argv[ii][0] == 'b') {
			break_addr = 0x0100;
			if (isalnum(argv[ii][1]))
				break_addr = strtol(argv[ii] + 1, NULL, 16);
			printf("Breakpoint at address $%04X\n", break_addr);
		}
		else if (argv[ii][0] == 'l') {
			logfile = fopen(argv[ii] + 1, "w");
			if (!logfile)
				fprintf(stderr, "Error: could not open log file %s\n", argv[ii] + 1);
		}
		if (argv[ii][0] == 's') {
			if (isdigit(argv[ii][1]))
				start_logging_instrnr = atoi(argv[ii] + 1);
		}
		else if (argv[ii][0] == 'm')
			max_instr = atoi(argv[ii] + 1);
	}

	struct rom* rom = rom_create(argv[argc - 1]);
	if (!rom->data)
		return 2;
	mem_connect_rom(mem, rom);
	printf("Loaded ROM %s. Type: %02X\n", argv[argc-1], rom_get_type(rom));

	struct timers* timers = timers_create(mem);
	struct cpu* cpu = cpu_create(mem, timers);

	bool break_hit = false;
	bool done = false;
	int nr_instr_ran = 0;

	clock_t start = clock();

	while (!done && !cpu_is_stopped(cpu)) {
		timers_clock(timers);
		
		if (cpu->cycles_left == 0) { // new instruction coming up
			if (logfile && nr_instr_ran + 1 >= start_logging_instrnr) {
#ifdef EXTRA_LOGGING
				fprintf(logfile, "%8u %d ", nr_instr_ran + 1, cpu->mcycles);
#endif
				print_state_gbdoctor(cpu, logfile);
			}

			if (break_instrnr > 0 && nr_instr_ran + 1 == break_instrnr)
				break_hit = true;
			if (break_addr >= 0 && cpu->PC == break_addr)
				break_hit = true;
			if (mem->io[0x03] && cpu->PC >= 0xC000)
				break_hit = true;

			if (break_hit) {
				printf("\nInstr #: %u\n", nr_instr_ran + 1);
				cpu_print_info(cpu);
				char buf[80];
				fgets(buf, 80, stdin);
				if (buf[0] == 'q')
					done = true;
				else if (buf[0] == 'c')
					break_hit = false;
				else if (buf[0] == 'd') {
					u16 disp_addr = strtol(buf + 1, NULL, 16);
					printf("Mem content: $%02X\n", mem_read(mem, disp_addr));
				}
				else if (buf[0] == 'l' && !logfile) {
					// Remove newline
					char* bb = buf + 1;
					while (*bb && *bb != '\n')
						++bb;
					*bb = '\0';
					break_hit = false;
					logfile = fopen(buf + 1, "w");
					if (!logfile)
						fprintf(stderr, "Error: could not open log file %s\n", buf + 1);
				}
			}

			cpu_clock_cycle(cpu); // this one does an actual instruction

			++nr_instr_ran;
			if (nr_instr_ran == max_instr)
				done = true;
		}
		else
			cpu_clock_cycle(cpu);
	}

	clock_t end = clock();
	double elapsed_time = (double)(end - start) / CLOCKS_PER_SEC;

	printf("Instructions executed: %d\n", nr_instr_ran);
	printf("Clock speed: %4.3f MHz (T-cycles / sec)\n", ((double)(4 * cpu->mcycles)) / elapsed_time / 1.0e6);

	if (logfile)
		fclose(logfile);

	timers_destroy(timers);
	cpu_destroy(cpu);
	rom_destroy(rom);
	mem_destroy(mem);
	return 0;
}

