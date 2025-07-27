// TODO: Clean this up: introduce gameboy.h (gameboy "object")
// TODO: move to more traditional style of cmd line arguments
#include <bits/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include <raylib.h>

#include "rom.h"
#include "mem.h"
#include "src/common.h"
#include "timers.h"
#include "mcycle.h"
#include "cpu.h"
#include "ppu.h"

// window size
const int imgWidth  = 160;
const int imgHeight = 144;
const int pixScale  = 4;
const int winWidth  = pixScale * imgWidth;
const int winHeight = pixScale * imgHeight;

const unsigned int fps = 60;
const unsigned int mcycles_per_second = 1024 * 1024; // M-cycle speed: 2^22 / 4
const unsigned int mcycles_per_frame = mcycles_per_second / fps;

// palette
struct limeguy_color rgba_palette[] = {
	{.r=0x7C, .g=0x7A, .b=0x48, .a=0xFF},  // 00: #7C7A48
	{.r=0x66, .g=0x6E, .b=0x4A, .a=0xFF},  // 01: #666E4A
	{.r=0x57, .g=0x66, .b=0x4F, .a=0xFF},  // 10: #57664F
	{.r=0x58, .g=0x5C, .b=0x47, .a=0xFF},  // 11: #585C47
	{.r=0x94, .g=0x75, .b=0x20, .a=0xFF}   // off:#947520
};

#define EXTRA_LOGGING

// cmd line params
char* logfile_name = NULL;
unsigned int break_instrnr = 0; // see usage
int break_addr = -1;
unsigned int max_instr = 0;
unsigned int max_mcycles = 0;
unsigned int start_logging_instrnr = 0;
bool have_graphics = true; // if false, does not even open window

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

void print_usage(char* progname) {
	printf("Usage: %s [[b]addr_hex] [[i]instr#] [lLogfile] [sinstr#] [m????] [M????] [n] <romfile>\n", progname);
	printf("  b option: breakpoint at address (default: $0100)\n");
	printf("  i option: breakpoint at from instr# (default: 1)\n");
	printf("  l option: game boy doctor output enable (status line after each instr)\n");
	printf("  s option: start logging from instr$ given\n");
	printf("  m option: max # instructions to run\n");
	printf("  M option: max # Mcycles to run\n");
	printf("  n option: No graphics, no window\n");
	printf("---\n");
	printf("When in step-by-step mode:\n");
	printf("  q: exit program\n");
	printf("  c: continue (exit step-by-step mode)\n");
	printf("  dAddr_hex: display data at address addr_hex\n");
	printf("  lLogfile: start logging a la game boy doctor\n");
}

void parse_arguments(int argc, char* argv[]) { // uses globals for now
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
			logfile_name = argv[ii] + 1; // hacky
		}
		if (argv[ii][0] == 's') {
			if (isdigit(argv[ii][1]))
				start_logging_instrnr = atoi(argv[ii] + 1);
		}
		else if (argv[ii][0] == 'm')
			max_instr = atoi(argv[ii] + 1);
		else if (argv[ii][0] == 'M')
			max_mcycles = atoi(argv[ii] + 1);
		else if (argv[ii][0] == 'n')
			have_graphics = false;
	}
}

Texture load_dynamic_texture(int w, int h) {
	Image img = GenImageColor(w, h, BLACK);
	// set the image's format so it is guaranteed to be aligned with our pixel buffer format below
	ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	return LoadTextureFromImage(img);
}

int main(int argc, char* argv[]) {
	FILE* logfile = NULL;
	Texture dynamic_tex;
	u8* rgba_pixels = NULL;
	struct timespec start_time, end_time;

	if (argc <= 1) {
		print_usage(argv[0]);
		return 1;
	}

	parse_arguments(argc, argv);
	if (logfile_name != NULL) {
		logfile = fopen(logfile_name, "w");
		if (!logfile)
			fprintf(stderr, "Error: could not open log file %s\n", logfile_name);
	}

	if (have_graphics) { // init raylib window
    	InitWindow(winWidth, winHeight, "Dynamic texture");

		dynamic_tex = load_dynamic_texture(imgWidth, imgHeight);
		rgba_pixels = malloc(imgWidth * imgHeight * 4); // pixel data

    	SetTargetFPS(fps);
	}

	// Set up game boy hardware
	struct mem* mem = mem_create();

	struct rom* rom = rom_create(argv[argc - 1]);
	if (!rom->data)
		return 2;
	mem_connect_rom(mem, rom);
	printf("Loaded ROM %s. Type: %02X\n", argv[argc-1], rom_get_type(rom));

	struct timers* timers = timers_create(mem);
	struct ppu* ppu = ppu_create(mem);
	// struct ppu* ppu = NULL;

	struct mcycle* mcycle = mcycle_create(timers, ppu);
	struct cpu* cpu = cpu_create(mem, mcycle);

	bool break_hit = false;
	bool done = false;

	clock_gettime(CLOCK_REALTIME, &start_time);

	// "game" loop
	while (!done && !cpu_is_stopped(cpu)) {
		if (have_graphics)
			done = WindowShouldClose();

		// TODO: How to define the exact "frame" loop?
		// frame loop (run instrucutions until frame is done, or some max is exceeded)
		bool frame_done = false;
		cpu_reset_mcycle_frame(cpu);
		while (!done && !frame_done) {

			if (logfile && cpu->nr_instructions + 1 >= start_logging_instrnr) {
#ifdef EXTRA_LOGGING
				fprintf(logfile, "%8u ", cpu->nr_instructions + 1);
				if (ppu)
					fprintf(logfile, "%d,%d,%d ", ppu->xdot, ppu->ly, ppu->mode);
#endif
				print_state_gbdoctor(cpu, logfile);
			}

			// break point conditions
			if (break_instrnr > 0 && cpu->nr_instructions + 1 == break_instrnr)
				break_hit = true;
			if (break_addr >= 0 && cpu->PC == break_addr)
				break_hit = true;
			if (mem->io[0x03]) // unused IO addr used as break
				break_hit = true;
			// TODO: break on LD B,B (mooneye test suite)

			if (break_hit) {
				printf("\nInstr #: %u\n", cpu->nr_instructions + 1);
				cpu_print_info(cpu);
				char buf[80];
				fgets(buf, 80, stdin);
				if (buf[0] == 'q')
					done = true; // causes program to exit
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
					break_hit = false; // continue execution normally
					logfile = fopen(buf + 1, "w");
					if (!logfile)
						fprintf(stderr, "Error: could not open log file %s\n", buf + 1);
				}
			}

			cpu_run_instruction(cpu);

			// exit conditions
			if (cpu->nr_instructions == max_instr || (max_mcycles && cpu->nr_mcycles >= max_mcycles))
				done = true;
			if (cpu_get_mcycle_frame(cpu) > 2 * mcycles_per_frame) // loads of margin...
				frame_done = true;
			if (ppu && ppu_frame_is_done(ppu)) {
				ppu_reset_frame_done(ppu);
				frame_done = true;
			}
		} // end frame loop

		if (have_graphics) {
			ppu_lcd_to_rgba(ppu, rgba_pixels, imgWidth, imgHeight, rgba_palette);
			UpdateTexture(dynamic_tex, rgba_pixels);
        	BeginDrawing();
        		ClearBackground(RAYWHITE); // Not needed
				DrawTextureEx(dynamic_tex, (Vector2) {0, 0}, 0.0f, (float)pixScale, WHITE);
        		DrawText(TextFormat("FPS: %d", GetFPS()), 10, 10, 30, RED);
        		DrawText(TextFormat("Mcycles this frame: %d", cpu_get_mcycle_frame(cpu)), 10, 40, 30, RED);
        	EndDrawing();
		}
	} // end game loop

	clock_gettime(CLOCK_REALTIME, &end_time);
	double elapsed_time = (double)(end_time.tv_sec - start_time.tv_sec) +
		(double)(end_time.tv_nsec - start_time.tv_sec) * 1.0E-9;

	// print execution / debug summary
	printf("Instructions executed: %d\n", cpu->nr_instructions);
	printf("Frames drawn: %u\n", ppu->nr_frames);
	printf("Elapsed time: %fs (realtime clock), %fs (frames)\n", elapsed_time, (double)ppu->nr_frames * (1.0 / (double)fps));
	printf("M-cycles: %d; T-cycles: %d\n", cpu->nr_mcycles, 4 * cpu->nr_mcycles);
	printf("Clock speed: %4.3f MHz (T-cycles / sec)\n", ((double)(4 * cpu->nr_mcycles)) / elapsed_time / 1.0e6);
	printf("Interrupt count:\n");
	printf("  Vblank: %d\n", cpu->interrupt_count[0]);
	printf("  LCD:    %d\n", cpu->interrupt_count[1]);
	printf("  Timer:  %d\n", cpu->interrupt_count[2]);
	printf("  Serial: %d\n", cpu->interrupt_count[3]);
	printf("  Joypad: %d\n", cpu->interrupt_count[4]);

	// clean-up
	if (logfile)
		fclose(logfile);
	ppu_destroy(ppu);
	timers_destroy(timers);
	cpu_destroy(cpu);
	rom_destroy(rom);
	mem_destroy(mem);
	if (have_graphics) {
    	free(rgba_pixels);
    	UnloadTexture(dynamic_tex);
    	CloseWindow();                  // Close window and OpenGL context
	}
	return 0;
}

