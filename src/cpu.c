#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cpu.h"
#include "mem.h"
#include "mcycle.h"

#define OPCODE_PREFIX 0xCB

static
void cpu_init(struct cpu* cpu) {
	// game boy doctor state:
	for (int ii = REG_A; ii <= REG_L; ++ii)
		cpu->regs[ii] = 0;
	cpu->SP = 0x0FFFE;
	cpu->PC = 0x0100;
	cpu->flags.Z = false;
	cpu->flags.N = false;
	cpu->flags.H = false;
	cpu->flags.C = false;
	cpu->ime = false;
	cpu->ei_initiated = false;

	cpu->cycles_left = 0;
	cpu->halted = false;
	cpu->haltbug = false;
	cpu->stopped = false;

	cpu->nr_mcycles = 0;
	cpu->nr_mcycles_frame = 0; // resetable version
	cpu->nr_instructions = 0;
	for (int ii = 0; ii < 5; ++ii)
		cpu->interrupt_count[ii] = 0;
}

void cpu_initregs_gbdoctor(struct cpu* cpu) {
	cpu_init(cpu);
	// game boy doctor state:
	cpu->regs[REG_A] = 0x01;
	cpu->regs[REG_C] = 0x13;
	cpu->regs[REG_E] = 0xD8;
	cpu->regs[REG_H] = 0x01;
	cpu->regs[REG_L] = 0x4D;
	cpu->flags.Z = true;
	cpu->flags.H = true;
	cpu->flags.C = true;
}

void cpu_initregs_dmg0(struct cpu* cpu) {
	cpu_init(cpu);
	cpu->regs[REG_A] = 0x01;
	cpu->regs[REG_B] = 0xFF;
	cpu->regs[REG_C] = 0x13;
	cpu->regs[REG_E] = 0xC1;
	cpu->regs[REG_H] = 0x84;
	cpu->regs[REG_L] = 0x03;
}

struct cpu* cpu_create(struct mem* mem, struct mcycle* mcycle) {
	struct cpu* cpu = malloc(sizeof(struct cpu));
	cpu->mem = mem;
	cpu->mcycle = mcycle;
	cpu_init(cpu);
	return cpu;
}

void cpu_destroy(struct cpu* cpu) {
	free(cpu);
}

bool cpu_is_stopped(struct cpu* cpu) {
	return cpu->stopped;
}

void cpu_reset_mcycle_frame(struct cpu* cpu) {
	cpu->nr_mcycles_frame = 0;
}

unsigned int cpu_get_mcycle_frame(struct cpu* cpu) {
	return cpu->nr_mcycles_frame;
}

void cpu_print_state_gbdoctor(struct cpu* cpu, FILE* logfile) {
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

static struct instruction opcode_lookup[512];   // contains instruction info includinng jump table; initialized later

static
u16 bytes_to_word(u8 msbyte, u8 lsbyte) {
	return (((u16)msbyte) << 8) | (u16)lsbyte;
}

static
void word_to_bytes(u16 w, u8* msbyte, u8* lsbyte) {
	*msbyte = w >> 8;
	*lsbyte = w & 0x0FF;
}

static
u8 flags_to_byte(struct flags *f) {
	return (f->Z << 7) | (f->N << 6) | (f->H << 5) | (f->C << 4);
}

static
void byte_to_flags(struct flags *f, u8 b) {
	f->Z = ((b >> 7) & 1) == 1;
	f->N = ((b >> 6) & 1) == 1;
	f->H = ((b >> 5) & 1) == 1;
	f->C = ((b >> 4) & 1) == 1;
}

static
int cpu_get_operand_size(enum op_type tp) {
	return ((REG_BC <= tp && tp <= REG_SP) || tp == IMM16) ? 16 : 8;
}

// TODO: Move into cpu_clock_cycle fn
static
void cpu_mcycle(struct cpu* cpu) {
// Adds one more clock cycle, and calls periperal clock cycle fns
	++cpu->nr_mcycles;
	++cpu->nr_mcycles_frame;
	--cpu->cycles_left;
	mcycle_tick(cpu->mcycle);
}

static
u8 cpu_memread_cycle(struct cpu* cpu, u16 addr) {
	cpu_mcycle(cpu);
	return mem_read(cpu->mem, addr);
}

static
u16 cpu_memread16_cycle(struct cpu* cpu, u16 addr) {
	cpu_mcycle(cpu);
	u8 lsbyte = mem_read(cpu->mem, addr++);
	cpu_mcycle(cpu);
	u8 msbyte = mem_read(cpu->mem, addr);
	return (((u16)msbyte) << 8) | (u16)lsbyte;
}

static
void cpu_memwrite_cycle(struct cpu* cpu, u16 addr, u8 value) {
	cpu_mcycle(cpu);
	mem_write(cpu->mem, addr, value);
}

void cpu_memwrite16_cycle(struct cpu* cpu, u16 addr, u16 value) {
	cpu_mcycle(cpu);
	mem_write(cpu->mem, addr++, value & 0x00FF);
	cpu_mcycle(cpu);
	mem_write(cpu->mem, addr, value >> 8);
}

static
int cpu_get_operand(struct cpu* cpu, enum op_type tp) {
	// TODO: Use jump table for this function instead?
	if (tp <= REG_L)
		return cpu->regs[tp];
	if (tp <= REG_HL) {
		int idx = (tp - REG_BC) * 2 + REG_B;
		return bytes_to_word(cpu->regs[idx], cpu->regs[idx + 1]);
	}
	if (tp == REG_AF)
		return bytes_to_word(cpu->regs[REG_A], flags_to_byte(&cpu->flags));
	if (tp == REG_SP)
		return cpu->SP;
	if (tp <= MEM_HL) {
		int idx = (tp - MEM_BC) * 2 + REG_B;
		u16 addr = bytes_to_word(cpu->regs[idx], cpu->regs[idx + 1]);
		return cpu_memread_cycle(cpu, addr);
	}
	if (tp <= MEM_HLD) {
		u16 addr = bytes_to_word(cpu->regs[REG_H], cpu->regs[REG_L]);
		u16 new_hl = tp == MEM_HLI ? addr + 1 : addr - 1;
		word_to_bytes(new_hl, &cpu->regs[REG_H], &cpu->regs[REG_L]);
		return cpu_memread_cycle(cpu, addr);
	}
	if (tp == IMM8)
		return cpu_memread_cycle(cpu, cpu->PC++);
	if (tp == IMM16) {
		u16 val = cpu_memread16_cycle(cpu, cpu->PC);
		cpu->PC += 2;
		return val;
	}
	if (tp == MEM_IMM8) {
		u16 addr = 0x0FF00 + cpu_memread_cycle(cpu, cpu->PC++);
		return cpu_memread_cycle(cpu, addr);
	}
	if (tp == MEM_IMM16) {
		u16 addr = cpu_memread16_cycle(cpu, cpu->PC);
		cpu->PC += 2;
		return cpu_memread_cycle(cpu, addr);
	}
	/* Special case: partly handled in LD instr itself */
	if (tp == SP_IMM8) {
		u8 offs = cpu_memread_cycle(cpu, cpu->PC++);
		// convert to signed int
		return (int)((i8)offs);
	}
	if (tp == MEM_C) {
		u16 addr = 0x0FF00 + cpu->regs[REG_C];
		return cpu_memread_cycle(cpu, addr);
	}
	if (tp >= LIT0 && tp <= LIT7)
		return tp - LIT0;
	fprintf(stderr, "cpu_get_operand: unreachable (tp = %d)\n", tp);
	return 0;
}

static
void cpu_set_operand(struct cpu* cpu, enum op_type tp, int val) {
	// TODO: Use jump table for this function instead?
	//printf("cpu_set_operand tp=%u val=$%u\n", tp, val);
	if (tp <= REG_L)
		cpu->regs[tp] = val;
	else if (tp <= REG_HL) {
		int idx = (tp - REG_BC) * 2 + REG_B;
		word_to_bytes(val, &cpu->regs[idx], &cpu->regs[idx + 1]);
	}
	else if (tp == REG_AF) {
		u8 f;
		word_to_bytes(val, &cpu->regs[REG_A], &f);
		byte_to_flags(&cpu->flags, f);
	}
	else if (tp == REG_SP)
		cpu->SP = val;
	else if (tp <= MEM_HL) {
		int idx = (tp - MEM_BC) * 2 + REG_B;
		u16 addr = bytes_to_word(cpu->regs[idx], cpu->regs[idx + 1]);
		cpu_memwrite_cycle(cpu, addr, val);
	}
	else if (tp <= MEM_HLD) {
		u16 addr = bytes_to_word(cpu->regs[REG_H], cpu->regs[REG_L]);
		int new_hl = tp == MEM_HLI ? addr + 1 : addr - 1;
		word_to_bytes(new_hl, &cpu->regs[REG_H], &cpu->regs[REG_L]);
		cpu_memwrite_cycle(cpu, addr, val);
	}
	else if (tp == MEM_IMM8) {
		u16 addr = 0x0FF00 + ((u16)cpu_memread_cycle(cpu, cpu->PC++) & 0x0FF);
		cpu_memwrite_cycle(cpu, addr, val);
	}
	else if (tp == MEM_IMM16) {
		u16 addr = cpu_memread16_cycle(cpu, cpu->PC);
		cpu->PC += 2;
		cpu_memwrite_cycle(cpu, addr, val);
	}
	else if (tp == MEM16B_IMM16) {
		u16 addr = cpu_memread16_cycle(cpu, cpu->PC);
		cpu->PC += 2;
		cpu_memwrite16_cycle(cpu, addr, val);
	}
	else if (tp == MEM_C) {
		u16 addr = 0x0FF00 + ((u16)cpu->regs[REG_C] & 0x0FF);
		cpu_memwrite_cycle(cpu, addr, val);
	}
	else
		fprintf(stderr, "cpu_set_operand: unreachable (tp = %d)\n", tp);
}

static
bool cpu_check_cond(struct cpu* cpu, enum op_type tp) {
	switch (tp) {
		case COND_NZ:
			return !cpu->flags.Z;
		case COND_Z:
			return cpu->flags.Z;
		case COND_NC:
			return !cpu->flags.C;
		case COND_C:
			return cpu->flags.C;
		default:
			return true;
	}
}

static
void cpu_do_interrupt(struct cpu* cpu, int nr) {
	++cpu->interrupt_count[nr];
	mem_clear_interrupt_flag(cpu->mem, nr);
	cpu->ime = false;
	cpu->SP -= 2;
	cpu_mcycle(cpu);
	cpu_mcycle(cpu);
	cpu_memwrite16_cycle(cpu, cpu->SP, cpu->PC);
	cpu->PC = 0x040 + nr * 8;
	cpu_mcycle(cpu);
	cpu->cycles_left = 0;
}

void cpu_run_instruction(struct cpu* cpu) { // process 1 M-cycle
	++cpu->nr_instructions; // increased here already, to make compatible with older versions of limeguy

	if (cpu->stopped)
		return; // TODO: 

	// check interrupt
	if (cpu->ime || cpu->halted) {
		u16 interrupts = mem_get_active_interrupts(cpu->mem);
		for (int bitnr = 0; interrupts != 0 && bitnr < 5; ++bitnr) {
			if (interrupts & (1 << bitnr)) {
				cpu->halted = false;
				if (cpu->ime) {
					cpu_do_interrupt(cpu, bitnr);
					return;
				}
			}
		}
	}

	if (cpu->halted) {
		cpu_mcycle(cpu);
		return;
	}

	if (cpu->ei_initiated) { // EI instruction delay TODO: Test this
		cpu->ime = true;
		cpu->ei_initiated = false;
	}

	// read next instruction
	u16 opcode = cpu_memread_cycle(cpu, cpu->PC) & 0x0FF; // expand width
	// halt bug:
	cpu->PC = cpu->haltbug ? cpu->PC : cpu->PC + 1;
	cpu->haltbug = false;

	bool prefix = opcode == OPCODE_PREFIX;
	if (prefix)
		opcode = cpu_memread_cycle(cpu, cpu->PC++) & 0x0FF; // prefix: read next opcode
	struct instruction* instr = &opcode_lookup[opcode + (prefix ? 256 : 0)];
	cpu->cycles_left = instr->cycles - 1; // -1 for this cycle itself
	// For jumps/calls/rets, we correct in the instruction function when jump not taken

	// call instr function from jump table
	instr->func(cpu, instr);

	while (cpu->cycles_left) {
		cpu_mcycle(cpu);
	}
}

static
void cpu_fprint_operand(struct cpu* cpu, enum op_type tp, FILE* stream) {
	char* regnames8 = "ABCDEHL";
	if (tp == NIL || tp == COND_NIL)
		return;
	if (tp <= REG_L)
		fprintf(stream,"%c", regnames8[tp]);
	else if (tp <= REG_HL) {
		int idx = (tp - REG_BC) * 2 + REG_B;
		fprintf(stream,"%c%c", regnames8[idx], regnames8[idx + 1]);
	}
	else if (tp == REG_AF)
		fprintf(stream,"AF");
	else if (tp == REG_SP)
		fprintf(stream,"SP");
	else if (tp <= MEM_HL) {
		int idx = (tp - MEM_BC) * 2 + REG_B;
		fprintf(stream,"[%c%c]", regnames8[idx], regnames8[idx + 1]);
	}
	else if (tp <= MEM_HLD) {
		fprintf(stream,"[HL%c]", tp == MEM_HLI ? '+' : '-');
	}
	else if (tp == IMM8) {
		u16 val = mem_read(cpu->mem, cpu->PC++) & 0x0FF;
		fprintf(stream,"$%02X", val);
	}
	else if (tp == IMM16) {
		u16 val = mem_read16(cpu->mem, cpu->PC);
		cpu->PC += 2;
		fprintf(stream,"$%04X", val);
	}
	else if (tp == MEM_IMM8) {
		u16 val = mem_read(cpu->mem, cpu->PC++) & 0x0FF;
		fprintf(stream,"[$FF00 + $%02X]", val);
	}
	else if (tp == MEM_IMM16 || tp == MEM16B_IMM16) {
		u16 addr = mem_read16(cpu->mem, cpu->PC);
		cpu->PC += 2;
		fprintf(stream,"[$%04X]", addr);
	}
	else if (tp == SP_IMM8) {
		i8 offs = (i8)mem_read(cpu->mem, cpu->PC++);
		if (offs >= 0)
			fprintf(stream,"SP+%d", offs);
		else
			fprintf(stream,"SP-%d", -offs);
	}
	else if (tp == MEM_C)
		fprintf(stream,"[$FF00 + C]");
	else if (tp == COND_NZ)
		fprintf(stream,"NZ");
	else if (tp == COND_Z)
		fprintf(stream,"Z");
	else if (tp == COND_NC)
		fprintf(stream,"NC");
	else if (tp == COND_C)
		fprintf(stream,"C");
	else if (tp >= LIT0 && tp <= LIT7)
		fprintf(stream,"%d", tp - LIT0);
	else
		fprintf(stderr, "cpu_print_operand: unreachable (tp = %d)\n", tp);
}


// DEBUG

u8 cpu_get_opcode_at_pc(struct cpu* cpu) {
	return mem_read(cpu->mem, cpu->PC);
}

void cpu_fprint_instr_at_pc(struct cpu* cpu, FILE* stream) {
	u16 pc = cpu->PC;
	u16 opcode = mem_read(cpu->mem, cpu->PC++) & 0x0FF; // expand width
	bool prefix = opcode == OPCODE_PREFIX;
	if (prefix)
		opcode = mem_read(cpu->mem, cpu->PC++) & 0x0FF; // prefix: read next opcode
	struct instruction* instr = &opcode_lookup[opcode + (prefix ? 256 : 0)];
	fprintf(stream, "%s ", instr->mnemonic);
	if (!strcmp(instr->mnemonic, "RST")) // RST does not use op_type like other instructions
		fprintf(stream, "$%02X", 8 * (instr->op1 - LIT0));
	else {
		cpu_fprint_operand(cpu, instr->op1, stream);
		if (instr->op2 != NIL) {
			if (instr->op1 != COND_NIL)
				fprintf(stream, ", ");
			cpu_fprint_operand(cpu, instr->op2, stream);
		}
	}
	fprintf(stream, "\n");
	cpu->PC = pc;
}

void cpu_print_info(struct cpu* cpu) {
	char* regnames8 = "ABCDEHL";
	printf("Flags: Z=%d, N=%d, H=%d, C=%d   IME=%d\n", cpu->flags.Z?1:0,
			cpu->flags.N?1:0, cpu->flags.H?1:0, cpu->flags.C?1:0, cpu->ime?1:0); 
	for (int ii = 0; ii < 7; ++ii)
		printf("%c: $%02X  ", regnames8[ii], cpu->regs[ii] & 0x0FF); // &0xff: properly show neg nrs
	/*
	i8 f = flags_to_byte(&cpu->flags);
	printf("\nAF: %04X  ", bytes_to_word(cpu->regs[REG_A], f));
	printf("BC: %04X  ", bytes_to_word(cpu->regs[REG_B], cpu->regs[REG_C]));
	printf("DE: %04X  ", bytes_to_word(cpu->regs[REG_D], cpu->regs[REG_E]));
	printf("HL: %04X  ", bytes_to_word(cpu->regs[REG_H], cpu->regs[REG_L]));
	*/
	printf("\nSP: $%04X\n", cpu->SP);
	printf("PC: $%04X ==> ", cpu->PC);
	cpu_fprint_instr_at_pc(cpu, stdout);
	printf("Cycles left: %u\n", cpu->cycles_left);
	// TODO: interrupt flag?
}

// Instructions
void ADC(struct cpu* cpu, struct instruction* instr) {
	i8 op = (i8)cpu_get_operand(cpu, instr->op2);
	i8 c = cpu->flags.C ? 1 : 0;
	cpu->flags.N = false;
	cpu->flags.H = (cpu->regs[REG_A] & 0x0F) + (op & 0x0F) + c > 0x0F;
	cpu->flags.C = ((u16)cpu->regs[REG_A] & 0xFF) + ((u16)op & 0xFF) + c > 0xFF;
	cpu->regs[REG_A] += (u8)(op + c);
	cpu->flags.Z = cpu->regs[REG_A] == 0;
}

static void ADD(struct cpu* cpu, struct instruction* instr) {
	cpu->flags.N = false;
	if (instr->op1 == REG_SP) {
		i8 op = (i8)cpu_get_operand(cpu, instr->op2);
		cpu->flags.H = (cpu->SP & 0x0F) + (op & 0x0F) > 0x0F;
		cpu->flags.C = (cpu->SP & 0xFF) + ((u16)op & 0xFF) > 0xFF;
		cpu->SP += op;
		cpu->flags.Z = false;
	}
	else if (cpu_get_operand_size(instr->op1) == 16) {
		u16 op = cpu_get_operand(cpu, instr->op2);
		u16 target = cpu_get_operand(cpu, instr->op1); // HL or SP
		cpu->flags.H = (target & 0x0FFF) + (op & 0x0FFF) > 0xFFF;
		cpu->flags.C = ((uint32_t)target & 0xFFFF) + ((uint32_t)op & 0xFFFF) > 0xFFFF;
		cpu_set_operand(cpu, instr->op1, target + op);
	}
	else {
		i8 op = (i8)cpu_get_operand(cpu, instr->op2);
		cpu->flags.H = (cpu->regs[REG_A] & 0x0F) + (op & 0x0F) > 0x0F;
		cpu->flags.C = ((u16)cpu->regs[REG_A] & 0xFF) + ((u16)op & 0xFF) > 0xFF;
		cpu->regs[REG_A] += op;
		cpu->flags.Z = cpu->regs[REG_A] == 0;
	}
}

static void AND(struct cpu* cpu, struct instruction* instr) {
	u8 op = cpu_get_operand(cpu, instr->op2);
	cpu->regs[REG_A] &= op;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
	cpu->flags.N = false;
	cpu->flags.H = true;
	cpu->flags.C = false;
}

static void BIT(struct cpu* cpu, struct instruction* instr) {
	u8 b  = cpu_get_operand(cpu, instr->op1);
	u8 op = cpu_get_operand(cpu, instr->op2);
	cpu->flags.Z = ((op >> b) & 1) == 0;
	cpu->flags.N = false;
	cpu->flags.H = true;
}

static void CALL(struct cpu* cpu, struct instruction* instr) {
/*
; CALL cc, nn is expected to have the following timing:
; M = 0: instruction decoding
; M = 1: nn read: memory access for low byte
; M = 2: nn read: memory access for high byte
; M = 3: internal delay
; M = 4: PC push: memory access for high byte
; M = 5: PC push: memory access for low byte
*/
	u16 addr = cpu_get_operand(cpu, instr->op2);
	bool cond = cpu_check_cond(cpu, instr->op1);
	if (cond) {
		cpu_mcycle(cpu); // extra cycle M=3
		cpu_mcycle(cpu);
		mem_write(cpu->mem, --cpu->SP, cpu->PC >> 8);
		//cpu_memwrite16_cycle(cpu, cpu->SP, cpu->PC);
		cpu_mcycle(cpu);
		mem_write(cpu->mem, --cpu->SP, cpu->PC & 0xFF);
		cpu->PC = addr;
	}
	else // TODO: Remove the need for cycles_left to begin with...
		cpu->cycles_left -= (instr->cycles - instr->cycles_alt);
}

static void CCF(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
	cpu->flags.N = false;
	cpu->flags.H = false;
	cpu->flags.C = !cpu->flags.C;
}

static void CP(struct cpu* cpu, struct instruction* instr) {
	u8 op = cpu_get_operand(cpu, instr->op2);
	cpu->flags.N = true;
	cpu->flags.H = (cpu->regs[REG_A] & 0x0F) < (op & 0x0F);
	cpu->flags.C = cpu->regs[REG_A] < op;
	cpu->flags.Z = cpu->regs[REG_A] == op;
}

static void CPL(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
	cpu->flags.N = true;
	cpu->flags.H = true;
	cpu->regs[REG_A] = ~cpu->regs[REG_A];
}

static void DAA(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
	// see: https://blog.ollien.com/posts/gb-daa/
    i8 offset = 0;
    u8 a = cpu->regs[REG_A];
    bool carry_out = false;
    if ( (!cpu->flags.N && (a & 0x0F) > 0x09) || cpu->flags.H) {
        offset |= 0x06;
    }
    if ( (!cpu->flags.N && a > 0x99) || cpu->flags.C) {
        offset |= 0x60;
		carry_out = true;
    }
	a = cpu->flags.N ? a - offset : a + offset;
	cpu->regs[REG_A] = a;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
	cpu->flags.H = false;
	cpu->flags.C = carry_out;
}

static void DEC(struct cpu* cpu, struct instruction* instr) {
	if (cpu_get_operand_size(instr->op1) == 16) {
		cpu_set_operand(cpu, instr->op1, cpu_get_operand(cpu, instr->op1) - 1);
	}
	else {
		u8 op = cpu_get_operand(cpu, instr->op1);
		cpu->flags.H = (op & 0x0F) == 0;
		cpu->flags.N = true;
		--op;
		cpu->flags.Z = op == 0;
		cpu_set_operand(cpu, instr->op1, op);
	}
}

static void DI(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
	cpu->ime = false;
}

static void EI(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
	cpu->ei_initiated = true;
}

static void HALT(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
	cpu->halted = true;
 	 // HALT called when interrupts pending: read next byte @ PC twice:
	cpu->haltbug = !cpu->ime && mem_get_active_interrupts(cpu->mem) != 0;
}

static void INC(struct cpu* cpu, struct instruction* instr) {
	if (cpu_get_operand_size(instr->op1) == 16) {
		cpu_set_operand(cpu, instr->op1, cpu_get_operand(cpu, instr->op1) + 1);
	}
	else {
		u8 op = cpu_get_operand(cpu, instr->op1);
		cpu->flags.H = (op & 0x0F) == 0x0F;
		cpu->flags.N = false;
		++op;
		cpu->flags.Z = op == 0;
		cpu_set_operand(cpu, instr->op1, op);
	}
}

static void JP(struct cpu* cpu, struct instruction* instr) {
	u16 addr = cpu_get_operand(cpu, instr->op2);
	bool cond = cpu_check_cond(cpu, instr->op1);
	cpu->PC = cond ? addr : cpu->PC;
	// correct cycles in case jump not taken
	/*
	if (cond && instr->op2 == IMM16) // not for JP HL
		cpu_mcycle(cpu);
	*/
	cpu->cycles_left -= cond ? 0 : (instr->cycles - instr->cycles_alt);
}

static void JR(struct cpu* cpu, struct instruction* instr) {
	i8 offs = (i8)cpu_get_operand(cpu, instr->op2);
	bool cond = cpu_check_cond(cpu, instr->op1);
	cpu->PC = cond ? cpu->PC + offs : cpu->PC;
	// correct cycles in case jump not taken
	cpu->cycles_left -= cond ? 0 : (instr->cycles - instr->cycles_alt);
}

static void LD(struct cpu* cpu, struct instruction* instr) {
	// Special case: LD HL, SP + imm8
	if (instr->op2 == SP_IMM8) {
		i8 imm8 = (i8)cpu_get_operand(cpu, instr->op2);
		cpu->flags.H = (cpu->SP & 0x0F) + (imm8 & 0x0F) > 0x0F;
		cpu->flags.C = (cpu->SP & 0xFF) + ((u16)imm8 & 0xFF) > 0xFF;
		cpu_set_operand(cpu, instr->op1, cpu->SP + imm8);
		cpu->flags.Z = false;
		cpu->flags.N = false;
	}
	else
		cpu_set_operand(cpu, instr->op1, cpu_get_operand(cpu, instr->op2));
}

static void LDH(struct cpu* cpu, struct instruction* instr) {
	cpu_set_operand(cpu, instr->op1, cpu_get_operand(cpu, instr->op2));
}

static void NOP(struct cpu* cpu, struct instruction* instr) {
	(void)cpu;
	(void)instr;
}

static void OR(struct cpu* cpu, struct instruction* instr) {
	u8 op = cpu_get_operand(cpu, instr->op2);
	cpu->regs[REG_A] |= op;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
	cpu->flags.N = false;
	cpu->flags.H = false;
	cpu->flags.C = false;
}

static void POP(struct cpu* cpu, struct instruction* instr) {
	u16 w = cpu_memread16_cycle(cpu, cpu->SP);
	cpu->SP += 2;
	cpu_set_operand(cpu, instr->op1, w);
}

static void PUSH(struct cpu* cpu, struct instruction* instr) {
	// passing mooneye push_timing.gb
	/*
; PUSH rr is expected to have the following timing:
; M = 0: instruction decoding
; M = 1: internal delay
; M = 2: memory access for high byte
; M = 3: memory access for low byte
	*/
	u16 w = cpu_get_operand(cpu, instr->op1);
	//printf("PUSH0: just did DMA at OAM %02X. DMA active: %d\n", (cpu->mem->dma_addr & 0xFF) - 1, cpu->mem->dma_active?1:0);
	cpu_mcycle(cpu);
	//printf("PUSH1: just did DMA at OAM %02X. DMA active: %d\n", (cpu->mem->dma_addr & 0xFF) - 1, cpu->mem->dma_active?1:0);
	cpu_mcycle(cpu);
	//printf("PUSH2: just did DMA at OAM %02X. DMA active: %d\n", (cpu->mem->dma_addr & 0xFF) - 1, cpu->mem->dma_active?1:0);
	//printf("PUSH2: writing high byte\n");
	mem_write(cpu->mem, --cpu->SP, w >> 8);
	cpu_mcycle(cpu);
	//printf("PUSH3: just did DMA at OAM %02X. DMA active: %d\n", (cpu->mem->dma_addr & 0xFF) - 1, cpu->mem->dma_active?1:0);
	//printf("PUSH3: writing low byte\n");
	mem_write(cpu->mem, --cpu->SP, w & 0xFF);
}

/*
static void PUSH(struct cpu* cpu, struct instruction* instr) {
	u16 w = cpu_get_operand(cpu, instr->op1);
	cpu->SP -= 2;
	cpu_memwrite16_cycle(cpu, cpu->SP, w);
}
*/

static void RES(struct cpu* cpu, struct instruction* instr) {
	u16 b = cpu_get_operand(cpu, instr->op1);
	u8 op = cpu_get_operand(cpu, instr->op2);
	op &= ~(1 << b);
	cpu_set_operand(cpu, instr->op2, op);
}

static void RET(struct cpu* cpu, struct instruction* instr) {
/*  RET: 4 cycles; RET cc: 5 or 2 cycles
; RET is expected to have the following timing:
; M = 0: instruction decoding
; M = 1: PC pop: memory access for low byte
; M = 2: PC pop: memory access for high byte
; M = 3: internal delay
; RET cc is expected to have the following timing:
; M = 0: instruction decoding
; M = 1: internal delay
; M = 2: PC pop: memory access for low byte
; M = 3: PC pop: memory access for high byte
; M = 4: internal delay
*/
	bool cond = cpu_check_cond(cpu, instr->op1);
	if (instr->op1 != COND_NIL)
		cpu_mcycle(cpu);
	if (cond) {
		cpu->PC = cpu_memread16_cycle(cpu, cpu->SP);
		cpu->SP += 2;
		cpu_mcycle(cpu);
	}
	else // TODO: remove need for cycles_left, make all instructions correct by design
		cpu->cycles_left -= (instr->cycles - instr->cycles_alt);
}

static void RETI(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
	cpu->ime = true;
	cpu->PC = cpu_memread16_cycle(cpu, cpu->SP);
	cpu->SP += 2;
}

static void RL(struct cpu* cpu, struct instruction* instr) {
    u16 r = cpu_get_operand(cpu, instr->op1) & 0x0FF;
	r = (r << 1) | (cpu->flags.C ? 1 : 0);
	cpu->flags.C = (r & 0x100) == 0x100;
	r &= 0x0FF;
	cpu_set_operand(cpu, instr->op1, r);
	cpu->flags.Z = r == 0;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void RLA(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
    u16 a = cpu->regs[REG_A]; // expand width
	a = (a << 1) | (cpu->flags.C ? 1 : 0);
	cpu->regs[REG_A] = a & 0x0FF;
	cpu->flags.C = (a & 0x100) == 0x100;
	cpu->flags.Z = false;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void RLC(struct cpu* cpu, struct instruction* instr) {
    u16 r = cpu_get_operand(cpu, instr->op1) & 0x0FF;
	r <<= 1;
	cpu->flags.C = (r & 0x100) == 0x100;
	r = (r & 0x0FF) | (cpu->flags.C ? 1 : 0);
	cpu_set_operand(cpu, instr->op1, r);
	cpu->flags.Z = r == 0;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void RLCA(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
    u16 a = cpu->regs[REG_A]; // expand width
	a <<= 1;
	cpu->flags.C = (a & 0x100) == 0x100;
	cpu->regs[REG_A] = (a & 0x0FF) | (cpu->flags.C ? 1 : 0);
	cpu->flags.Z = false;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void RR(struct cpu* cpu, struct instruction* instr) {
    u16 r = cpu_get_operand(cpu, instr->op1) & 0x0FF;
	r |= cpu->flags.C ? 0x100 : 0;
	cpu->flags.C = (r & 1) == 1;
	r >>= 1;
	cpu_set_operand(cpu, instr->op1, r);
	cpu->flags.Z = r == 0;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void RRA(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
    u16 a = cpu->regs[REG_A]; // expand width
	a |= cpu->flags.C ? 0x100 : 0;
	cpu->flags.C = (a & 1) == 1;
	cpu->regs[REG_A] = (a >> 1);
	cpu->flags.Z = false;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void RRC(struct cpu* cpu, struct instruction* instr) {
    u16 r = cpu_get_operand(cpu, instr->op1) & 0x0FF;
	cpu->flags.C = (r & 1) == 1;
	r = (r >> 1) | (cpu->flags.C ? 0x80 : 0);
	cpu_set_operand(cpu, instr->op1, r);
	cpu->flags.Z = r == 0;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void RRCA(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
    u16 a = cpu->regs[REG_A] & 0x0FF; // expand width
	cpu->flags.C = (a & 1) == 1;
	cpu->regs[REG_A] = (a >> 1) | (cpu->flags.C ? 0x80 : 0);
	cpu->flags.Z = false;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void RST(struct cpu* cpu, struct instruction* instr) {
/*
; RST is expected to have the following timing:
; M = 0: instruction decoding
; M = 1: internal delay
; M = 2: PC push: memory access for high byte
; M = 3: PC push: memory access for low byte
*/
	cpu_mcycle(cpu); // M=1 internal delay
	// save PC
	cpu_mcycle(cpu);
	mem_write(cpu->mem, --cpu->SP, cpu->PC >> 8);
	cpu_mcycle(cpu);
	mem_write(cpu->mem, --cpu->SP, cpu->PC & 0xFF);
	cpu->PC = 8 * cpu_get_operand(cpu, instr->op1);
}

static void SBC(struct cpu* cpu, struct instruction* instr) {
	i8 op = (i8)cpu_get_operand(cpu, instr->op2);
	i8 c = cpu->flags.C ? 1 : 0;
	cpu->flags.N = true;
	cpu->flags.H = (cpu->regs[REG_A] & 0x0F) < (op & 0x0F) + c;
	cpu->flags.C = ((u16)cpu->regs[REG_A] & 0xFF) < ((u16)op & 0xFF) + c;
	cpu->regs[REG_A] -= op + c;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
}

static void SCF(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
	cpu->flags.N = false;
	cpu->flags.H = false;
	cpu->flags.C = true;
}

static void SET(struct cpu* cpu, struct instruction* instr) {
	u16 b = cpu_get_operand(cpu, instr->op1);
	u8 op = cpu_get_operand(cpu, instr->op2);
	op |= (1 << b);
	cpu_set_operand(cpu, instr->op2, op);
}

static void SLA(struct cpu* cpu, struct instruction* instr) {
    u16 r = cpu_get_operand(cpu, instr->op1) & 0x0FF;
	cpu->flags.C = (r & 0x80) == 0x80;
	r = (r << 1) & 0x0FF;
	cpu_set_operand(cpu, instr->op1, r);
	cpu->flags.Z = r == 0;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void SRA(struct cpu* cpu, struct instruction* instr) {
    u16 r = cpu_get_operand(cpu, instr->op1) & 0x0FF;
	cpu->flags.C = (r & 1) == 1;
	r = (r & 0x80) | (r >> 1); // MSB unchanged
	cpu_set_operand(cpu, instr->op1, r);
	cpu->flags.Z = r == 0;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void SRL(struct cpu* cpu, struct instruction* instr) {
    u16 r = cpu_get_operand(cpu, instr->op1) & 0x0FF;
	cpu->flags.C = (r & 1) == 1;
	r >>= 1;
	cpu_set_operand(cpu, instr->op1, r);
	cpu->flags.Z = r == 0;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void STOP(struct cpu* cpu, struct instruction* instr) {
	(void)instr;
	printf("CPU: STOP instr at %04X\n", cpu->PC - 1);
	cpu->stopped = true;
}

static void SUB(struct cpu* cpu, struct instruction* instr) {
	i8 op = (i8)cpu_get_operand(cpu, instr->op2);
	cpu->flags.N = true;
	cpu->flags.H = (cpu->regs[REG_A] & 0x0F) < (op & 0x0F);
	cpu->flags.C = ((u16)cpu->regs[REG_A] & 0xFF) < ((u16)op & 0xFF);
	cpu->regs[REG_A] -= op;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
}

static void SWAP(struct cpu* cpu, struct instruction* instr) {
    u8 r = cpu_get_operand(cpu, instr->op1);
	r = (r >> 4) | ((r & 0x0F) << 4);
	cpu_set_operand(cpu, instr->op1, r);
	cpu->flags.Z = r == 0;
	cpu->flags.C = false;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void XOR(struct cpu* cpu, struct instruction* instr) {
	u8 op = cpu_get_operand(cpu, instr->op2);
	cpu->regs[REG_A] ^= op;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
	cpu->flags.N = false;
	cpu->flags.H = false;
	cpu->flags.C = false;
}

static void PREFIX(struct cpu* cpu, struct instruction* instr) {
	(void)cpu;
	(void)instr;
}

static void ILLEGAL(struct cpu* cpu, struct instruction* instr) {
	(void)cpu;
	(void)instr;
	printf("%s not implemented yet\n", instr->mnemonic);
}


#include "opcode_table.inc"
