// TODO: Remove non-debug related references to gameboy fields (like gameboy->ppu...)
// TODO: move to more traditional style of cmd line arguments
#include <bits/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include <raylib.h>

#include "gameboy.h"
#include "cpu.h"
#include "ppu.h"
#include "common.h"

// ld b,b -- break (mooneye test suite) -- comment if not desired
//#define INSTR_BREAK 0x40

// window size
const int imgWidth  = 160;
const int imgHeight = 144;
const int pixScale  = 6;
const int winWidth  = pixScale * imgWidth;
const int winHeight = pixScale * imgHeight;

const unsigned int fps = 60;
const unsigned int mcycles_per_second = 1024 * 1024; // M-cycle speed: 2^22 / 4
const unsigned int mcycles_per_frame = mcycles_per_second / fps;

// Keyboard mapping RayLib --> Gameboy button
struct keymap {
	int            key;
	enum gb_button button;
};

struct keymap keymaps[8] = {
	{ KEY_RIGHT, BUT_RIGHT },
	{ KEY_LEFT, BUT_LEFT },
	{ KEY_UP, BUT_UP },
	{ KEY_DOWN, BUT_DOWN },
	{ KEY_X, BUT_A },
	{ KEY_Z, BUT_B },
	{ KEY_S, BUT_SELECT },
	{ KEY_ENTER, BUT_START }
};

// palette
struct limeguy_color rgba_palette[] = {
	{.r=0x7C, .g=0x7A, .b=0x48, .a=0xFF},  // 00: #7C7A48
	{.r=0x66, .g=0x6E, .b=0x4A, .a=0xFF},  // 01: #666E4A
	{.r=0x57, .g=0x66, .b=0x4F, .a=0xFF},  // 10: #57664F
	{.r=0x58, .g=0x5C, .b=0x47, .a=0xFF},  // 11: #585C47
	{.r=0x94, .g=0x75, .b=0x20, .a=0xFF}   // off:#947520
};


// cmd line params
char* logfile_name = NULL;
unsigned int break_instrnr = 0; // see usage
int break_addr = -1;
unsigned int max_instr = 0;
unsigned int max_mcycles = 0;
unsigned int start_logging_instrnr = 0;
bool have_graphics = true; // if false, does not even open window

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
	printf("  mAddr_hex: display data at address addr_hex\n");
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

static
void get_keyboard_input(struct gameboy* gameboy) {
	for (int ii = 0; ii < (int)(sizeof(keymaps) / sizeof(keymaps[0])); ++ii)
		gameboy_set_button(gameboy, keymaps[ii].button, IsKeyDown(keymaps[ii].key));
}

static
void write_log_line(struct gameboy* gameboy, FILE* logfile) {
#ifdef EXTRA_LOGGING
		fprintf(logfile, "%8u ", gameboy->cpu->nr_instructions + 1);
		if (gameboy->ppu)
			fprintf(logfile, "%d,%d ", gameboy->ppu->xdot, gameboy->ppu->ly);
		// if (gameboy->timers)
		// 	fprintf(logfile, "%d,%d ", gameboy->timers->count_div, gameboy->mem->io[0x04]);
		//fprintf(logfile, "%d ", gameboy->timers->count_div);
#endif
		cpu_print_state_gbdoctor(gameboy->cpu, logfile);
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

	struct gameboy* gameboy = gameboy_create(argv[argc - 1]);

	if (have_graphics) { // init raylib window
    	InitWindow(winWidth, winHeight, "Dynamic texture");

		dynamic_tex = load_dynamic_texture(imgWidth, imgHeight);
		rgba_pixels = malloc(imgWidth * imgHeight * 4); // pixel data

    	SetTargetFPS(fps);
	}

	bool break_hit = false;
	bool done = false;

	clock_gettime(CLOCK_REALTIME, &start_time);

	// "game" loop
	while (!done && !cpu_is_stopped(gameboy->cpu)) {
		if (have_graphics)
			done = WindowShouldClose();

		// Input
		get_keyboard_input(gameboy);
		// debug key
		if (IsKeyPressed(KEY_D))
			break_hit = true;

		if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
			Vector2 mouse_pos = GetMousePosition();
			printf("x,y = %d, %d\n", (int)mouse_pos.x / pixScale, (int)mouse_pos.y / pixScale);
		}

		// TODO: How to define the exact "frame" loop?
		// frame loop (run instrucutions until frame is done, or some max is exceeded)
		bool frame_done = false;
		cpu_reset_mcycle_frame(gameboy->cpu);
		while (!done && !frame_done) { // Frame loop
			// TODO: clean-up!! Especially interactive breakpoint code

			if (logfile && gameboy->cpu->nr_instructions + 1 >= start_logging_instrnr)
				write_log_line(gameboy, logfile);

			// break point conditions
			if (break_instrnr > 0 && gameboy->cpu->nr_instructions + 1 == break_instrnr)
				break_hit = true;
			if (break_addr >= 0 && gameboy->cpu->PC == break_addr)
				break_hit = true;
			if (gameboy->mem->io[0x03] != 0xFF) // unused IO addr used as break, when not read as 0xFF
				break_hit = true;
#ifdef INSTR_BREAK
			if (cpu_get_opcode_at_pc(gameboy->cpu) == INSTR_BREAK) //break on LD B,B (mooneye test suite)
				break_hit = true;
#endif
			if (break_hit) {
				printf("\nInstr #: %u\n", gameboy->cpu->nr_instructions + 1);
				cpu_print_info(gameboy->cpu);
				ppu_print_info(gameboy->ppu);
				char buf[80];
				fgets(buf, 80, stdin);
				if (buf[0] == 'q')
					done = true; // causes program to exit
				else if (buf[0] == 'c')
					break_hit = false;
				else if (buf[0] == 'm') {
					u16 disp_addr = strtol(buf + 1, NULL, 16);
					printf("Mem content: $%02X\n", mem_read(gameboy->mem, disp_addr));
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

			cpu_run_instruction(gameboy->cpu);

			// exit conditions
			if (gameboy->cpu->nr_instructions == max_instr || (max_mcycles && gameboy->cpu->nr_mcycles >= max_mcycles))
				done = true;
			// we need to end frame at some point when LCD is off
			if (cpu_get_mcycle_frame(gameboy->cpu) > 2 * mcycles_per_frame) // loads of margin...
				frame_done = true;
			// use ppu signaling hand-shake to see if frame is done
			if (gameboy->ppu && ppu_frame_is_done(gameboy->ppu)) {
				ppu_reset_frame_done(gameboy->ppu);
				frame_done = true;
			}
		} // end frame loop

		if (have_graphics) {
			ppu_lcd_to_rgba(gameboy->ppu, rgba_pixels, imgWidth, imgHeight, rgba_palette);
			UpdateTexture(dynamic_tex, rgba_pixels);
        	BeginDrawing();
        		ClearBackground(RAYWHITE); // Not needed
				DrawTextureEx(dynamic_tex, (Vector2) {0, 0}, 0.0f, (float)pixScale, WHITE);
        		/*
        		DrawText(TextFormat("FPS: %d", GetFPS()), 10, 10, 30, RED);
        		DrawText(TextFormat("Mcycles this frame: %d", cpu_get_mcycle_frame(cpu)), 10, 40, 30, RED);
        		*/
        	EndDrawing();
		}
	} // end game loop

	clock_gettime(CLOCK_REALTIME, &end_time);
	double elapsed_time = (double)(end_time.tv_sec - start_time.tv_sec) +
		(double)(end_time.tv_nsec - start_time.tv_nsec) * 1.0E-9;

	// print execution / debug summary
	printf("Instructions executed: %d\n", gameboy->cpu->nr_instructions);
	printf("Frames drawn: %u\n", gameboy->ppu->nr_frames);
	printf("Elapsed time: %fs (realtime clock), %fs (frames)\n", elapsed_time, (double)gameboy->ppu->nr_frames * (1.0 / (double)fps));
	printf("M-cycles: %d; T-cycles: %d\n", gameboy->cpu->nr_mcycles, 4 * gameboy->cpu->nr_mcycles);
	printf("Clock speed: %4.3f MHz (T-cycles / sec)\n", ((double)(4 * gameboy->cpu->nr_mcycles)) / elapsed_time / 1.0e6);
	printf("Interrupt count:\n");
	printf("  Vblank: %d\n", gameboy->cpu->interrupt_count[0]);
	printf("  LCD:    %d\n", gameboy->cpu->interrupt_count[1]);
	printf("  Timer:  %d\n", gameboy->cpu->interrupt_count[2]);
	printf("  Serial: %d\n", gameboy->cpu->interrupt_count[3]);
	printf("  Joypad: %d\n", gameboy->cpu->interrupt_count[4]);

	// clean-up
	if (logfile)
		fclose(logfile);
	gameboy_destroy(gameboy);
	if (have_graphics) {
    	free(rgba_pixels);
    	UnloadTexture(dynamic_tex);
    	CloseWindow();                  // Close window and OpenGL context
	}
	return 0;
}

