#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rom.h"
#include "mem.h"
#include "cpu.h"
#include "timers.h"


// usage parameters
int step_by_step = 0; // see usage
bool gbdoctor = false;
int max_instr = 0;

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
		fprintf(logfile, "%02X%c", mem_read(cpu->mem, cpu->PC + offs) & 0x0FF, offs < 3 ? ',':'\n');
}

int main(int argc, char* argv[]) {
	FILE* logfile = NULL;

	if (argc <= 1) {
		printf("Usage: %s [[s]instr#] [dlogfile] [m????] <romfile>\n", argv[0]);
		printf("  s option: step by step execution, starting from instr# (default 1)\n");
		printf("  d option: game boy doctor output enable (status line after eaach instr)\n");
		printf("  m option: max #instructions to run\n");
		return 1;
	}

	struct mem* mem = mem_create();

	for (int ii = 1; ii < argc - 1; ++ii) {
		if (argv[ii][0] == 's') {
			step_by_step = 1;
			if (argv[ii][1] >= '0' && argv[ii][1] <= '9')
				step_by_step = atoi(argv[ii] + 1);
		}
		else if (argv[ii][0] == 'd') {
			gbdoctor = true;
			logfile = fopen(argv[ii] + 1, "w");
			if (!logfile)
				fprintf(stderr, "Error: could not open log file %s\n", argv[ii] + 1);
		}
		else if (argv[ii][0] == 'm')
			max_instr = atoi(argv[ii] + 1);
	}
	struct rom* rom = rom_create(argv[argc - 1]);
	if (!rom->data)
		return 2;
	mem_connect_rom(mem, rom);

	struct cpu* cpu = cpu_create(mem);
	struct timers* timers = timers_create(mem);


	bool force_break = false;
	bool done = false;
	int nr_instr_ran = 0;
	uint64_t m_cycles = 0;

	clock_t start = clock();

	while (!done) {
		timers_clock(timers);
		
		if (cpu->cycles_left == 0) {
			if (gbdoctor)
				print_state_gbdoctor(cpu, logfile);

			if (force_break || (step_by_step && nr_instr_ran >= step_by_step)) {
				cpu_print_info(cpu);
				char c;
				scanf("%c", &c);
				done = c == 'q';
			}

			cpu_clock_cycle(cpu); // this one does an actual instruction

			++nr_instr_ran;
			if (nr_instr_ran == max_instr)
				done = true;
		}
		else
			cpu_clock_cycle(cpu);

		++m_cycles;
	}

	clock_t end = clock();
	double elapsed_time = (double)(end - start) / CLOCKS_PER_SEC;

	printf("Clock speed: %4.3f MHz (T-cycles / sec)\n", ((double)(4 * m_cycles)) / elapsed_time / 1.0e6);

	if (logfile)
		fclose(logfile);

	timers_destroy(timers);
	cpu_destroy(cpu);
	rom_destroy(rom);
	mem_destroy(mem);
	return 0;
}

