#ifndef __CPU_H__
#define __CPU_H__

#include <stdio.h>
#include <stdbool.h>

#include "mem.h"
#include "typedefs.h"

#define NR_REGS 7


// TODO: any way to simplify / generalize this mess?
enum op_type {
	// REG_A .. REG_L also used as indices in regs[]
	REG_A = 0, REG_B, REG_C, REG_D, REG_E, REG_H, REG_L,
	// 16 bit regs
	REG_BC, REG_DE, REG_HL, REG_AF, REG_SP,
	// 16 bit [regs]
	MEM_BC, MEM_DE, MEM_HL, MEM_HLI, MEM_HLD,
	// Immediate
	IMM8, IMM16, MEM_IMM8, MEM_IMM16, MEM16B_IMM16, SP_IMM8,
	// Misc
	MEM_C,
	// Conditions
	COND_NZ, COND_Z, COND_NC, COND_C, COND_NIL,
	// Literal values (e.g. RST, SET, etc.)
	LIT0, LIT1, LIT2, LIT3, LIT4, LIT5, LIT6, LIT7,

	NIL       // no operand
};

struct flags {
	bool Z; // zero
	bool N; // sub flag (BCD)
	bool H; // half-carry (BCD)
	bool C; // carry
};

struct instruction; // declare for next part

struct cpu {
	// registers & flags
	u8           regs[NR_REGS];
	u16          SP;
	u16          PC;
	struct flags flags;

	bool         ime; // interrupt master enbl
	bool         ei_initiated; // helper for instruction delay of EI

	unsigned int cycles_left; // to keep track of instr cycles

	struct mem*  mem;
	struct mcycle* mcycle;

	bool halted;
	bool haltbug;

	bool stopped;


	// DEBUG
	int mcycles;
};

typedef void instr_fn(struct cpu* cpu, struct instruction* instr); // instruction function type

struct instruction {
	char*        mnemonic;
    instr_fn*    func;     // function to call
	enum op_type op1;
	enum op_type op2;
	int          cycles;
	int          cycles_alt; // for non-taken jumps/calls/rets
};


struct cpu* cpu_create(struct mem* mem, struct mcycle* mcycle);
void cpu_destroy(struct cpu* cpu);

void cpu_run_instruction(struct cpu* cpu);
bool cpu_is_stopped(struct cpu* cpu);

void cpu_print_info(struct cpu* cpu);
void cpu_fprint_instr_at_pc(struct cpu* cpu, FILE* stream);

#endif
