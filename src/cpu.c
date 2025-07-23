#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cpu.h"
#include "mem.h"

#define OPCODE_PREFIX 0xCB

struct instruction;

typedef void instr_fn(struct cpu* cpu, struct instruction* instr); // instruction function type

struct instruction {
	char*        mnemonic;
    instr_fn*    func;     // function to call
	enum op_type op1;
	enum op_type op2;
	int          cycles;
	int          cycles_alt; // for non-taken jumps/calls/rets
};

static
void cpu_init(struct cpu* cpu) {
	// game boy doctor state:
	cpu->regs[REG_A] = 0x01;
	cpu->regs[REG_B] = 0x00;
	cpu->regs[REG_C] = 0x13;
	cpu->regs[REG_D] = 0x00;
	cpu->regs[REG_E] = 0xD8;
	cpu->regs[REG_H] = 0x01;
	cpu->regs[REG_L] = 0x4D;
	cpu->SP = 0x0FFFE;
	cpu->PC = 0x0100;
	cpu->flags.Z = true;
	cpu->flags.N = false;
	cpu->flags.H = true;
	cpu->flags.C = true;
	cpu->ime = false;
	cpu->ei_initiated = false;

	cpu->cycles_left = 0;
	cpu->halted = false;
	cpu->haltbug = false;
	cpu->stopped = false;
}

struct cpu* cpu_create(struct mem* mem) {
	struct cpu* cpu = malloc(sizeof(struct cpu));
	cpu->mem = mem;
	cpu_init(cpu);
	return cpu;
}

void cpu_destroy(struct cpu* cpu) {
	free(cpu);
}

bool cpu_is_stopped(struct cpu* cpu) {
	return cpu->stopped;
}

static struct instruction opcode_lookup[512];   // contains instruction info includinng jump table; initialized later

static
u16 bytes_to_word(i8 msbyte, i8 lsbyte) {
	return (((u16)msbyte) << 8) | ((u16)lsbyte & 0x0FF);
}

static
void word_to_bytes(u16 w, i8* msbyte, i8* lsbyte) {
	*msbyte = w >> 8;
	*lsbyte = w & 0x0FF;
}

static
i8 flags_to_byte(struct flags *f) {
	return (f->Z << 7) | (f->N << 6) | (f->H << 5) | (f->C << 4);
}

static
void byte_to_flags(struct flags *f, i8 b) {
	f->Z = ((b >> 7) & 1) == 1;
	f->N = ((b >> 6) & 1) == 1;
	f->H = ((b >> 5) & 1) == 1;
	f->C = ((b >> 4) & 1) == 1;
}

static
int cpu_get_operand_size(struct cpu* cpu, enum op_type tp) {
	return ((REG_BC <= tp && tp <= REG_SP) || tp == IMM16) ? 16 : 8;
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
		return mem_read(cpu->mem, addr);
	}
	if (tp <= MEM_HLD) {
		u16 addr = bytes_to_word(cpu->regs[REG_H], cpu->regs[REG_L]);
		u16 new_hl = tp == MEM_HLI ? addr + 1 : addr - 1;
		word_to_bytes(new_hl, &cpu->regs[REG_H], &cpu->regs[REG_L]);
		return mem_read(cpu->mem, addr);
	}
	if (tp == IMM8)
		return mem_read(cpu->mem, cpu->PC++);
	if (tp == IMM16) {
		u16 val = mem_read16(cpu->mem, cpu->PC);
		cpu->PC += 2;
		return val;
	}
	if (tp == MEM_IMM8) {
		u16 addr = 0x0FF00 + ((u16)mem_read(cpu->mem, cpu->PC++) & 0x0FF);
		return mem_read(cpu->mem, addr);
	}
	if (tp == MEM_IMM16) {
		u16 addr = mem_read16(cpu->mem, cpu->PC);
		cpu->PC += 2;
		return mem_read(cpu->mem, addr);
	}
	/* Special case: partly handled in LD instr itself */
	if (tp == SP_IMM8) {
		i8 offs = mem_read(cpu->mem, cpu->PC++);
		return offs;
	}
	if (tp == MEM_C) {
		u16 addr = 0x0FF00 + ((u16)cpu->regs[REG_C] & 0x0FF);
		return mem_read(cpu->mem, addr);
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
		i8 f;
		word_to_bytes(val, &cpu->regs[REG_A], &f);
		byte_to_flags(&cpu->flags, f);
	}
	else if (tp == REG_SP)
		cpu->SP = val;
	else if (tp <= MEM_HL) {
		int idx = (tp - MEM_BC) * 2 + REG_B;
		u16 addr = bytes_to_word(cpu->regs[idx], cpu->regs[idx + 1]);
		mem_write(cpu->mem, addr, val);
	}
	else if (tp <= MEM_HLD) {
		u16 addr = bytes_to_word(cpu->regs[REG_H], cpu->regs[REG_L]);
		int new_hl = tp == MEM_HLI ? addr + 1 : addr - 1;
		word_to_bytes(new_hl, &cpu->regs[REG_H], &cpu->regs[REG_L]);
		mem_write(cpu->mem, addr, val);
	}
	else if (tp == MEM_IMM8) {
		u16 addr = 0x0FF00 + ((u16)mem_read(cpu->mem, cpu->PC++) & 0x0FF);
		mem_write(cpu->mem, addr, val);
	}
	else if (tp == MEM_IMM16) {
		u16 addr = mem_read16(cpu->mem, cpu->PC);
		cpu->PC += 2;
		mem_write(cpu->mem, addr, val);
	}
	else if (tp == MEM16B_IMM16) {
		u16 addr = mem_read16(cpu->mem, cpu->PC);
		cpu->PC += 2;
		mem_write16(cpu->mem, addr, val);
	}
	else if (tp == MEM_C) {
		u16 addr = 0x0FF00 + ((u16)cpu->regs[REG_C] & 0x0FF);
		mem_write(cpu->mem, addr, val);
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
	mem_clear_interrupt_flag(cpu->mem, nr);
	cpu->ime = false;
	cpu->cycles_left = 4;
	cpu->SP -= 2;
	mem_write16(cpu->mem, cpu->SP, cpu->PC);
	cpu->PC = 0x040 + nr * 8;
}

void cpu_clock_cycle(struct cpu* cpu) { // process 1 M-cycle
	if (cpu->stopped)
		return; // TODO: 

	if (cpu->cycles_left) {
		--cpu->cycles_left;
		return;
	}

	// check interrupt
	if (cpu->ime || cpu->halted) {
		u16 interrupts = mem_get_active_interrupts(cpu->mem);
		for (int bitnr = 0; interrupts != 0 && bitnr <= 4; ++bitnr) {
			if (interrupts & (1 << bitnr)) {
				cpu->halted = false;
				if (cpu->ime) {
					cpu_do_interrupt(cpu, bitnr);
					return;
				}
			}
		}
	}

	if (cpu->halted)
		return;

	if (cpu->ei_initiated) { // EI instruction delay
		cpu->ime = true;
		cpu->ei_initiated = false;
	}

	// read next instruction
	u16 opcode = mem_read(cpu->mem, cpu->PC) & 0x0FF; // expand width
	// halt bug:
	cpu->PC = cpu->haltbug ? cpu->PC : cpu->PC + 1;
	cpu->haltbug = false;
	bool prefix = opcode == OPCODE_PREFIX;
	if (prefix)
		opcode = mem_read(cpu->mem, cpu->PC++) & 0x0FF; // prefix: read next opcode
	struct instruction* instr = &opcode_lookup[opcode + (prefix ? 256 : 0)];
	cpu->cycles_left = instr->cycles - 1; // -1 for this cycle itself
	// For jumps/calls/rets, we correct in the instruction function when jump not taken

	// call instr function from jump table
	instr->func(cpu, instr);
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
		i8 offs = mem_read(cpu->mem, cpu->PC++);
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
	i8 op = cpu_get_operand(cpu, instr->op2);
	i8 c = cpu->flags.C ? 1 : 0;
	cpu->flags.N = false;
	cpu->flags.H = (cpu->regs[REG_A] & 0x0F) + (op & 0x0F) + c > 0x0F;
	cpu->flags.C = ((u16)cpu->regs[REG_A] & 0xFF) + ((u16)op & 0xFF) + c > 0xFF;
	cpu->regs[REG_A] += op + c;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
}

static void ADD(struct cpu* cpu, struct instruction* instr) {
	cpu->flags.N = false;
	if (instr->op1 == REG_SP) {
		i8 op = cpu_get_operand(cpu, instr->op2);
		cpu->flags.H = (cpu->SP & 0x0F) + (op & 0x0F) > 0x0F;
		cpu->flags.C = (cpu->SP & 0xFF) + ((u16)op & 0xFF) > 0xFF;
		cpu->SP += op;
		cpu->flags.Z = false;
	}
	else if (cpu_get_operand_size(cpu, instr->op1) == 16) {
		u16 op = cpu_get_operand(cpu, instr->op2);
		u16 target = cpu_get_operand(cpu, instr->op1); // HL or SP
		cpu->flags.H = (target & 0x0FFF) + (op & 0x0FFF) > 0xFFF;
		cpu->flags.C = ((uint32_t)target & 0xFFFF) + ((uint32_t)op & 0xFFFF) > 0xFFFF;
		cpu_set_operand(cpu, instr->op1, target + op);
	}
	else {
		i8 op = cpu_get_operand(cpu, instr->op2);
		cpu->flags.H = (cpu->regs[REG_A] & 0x0F) + (op & 0x0F) > 0x0F;
		cpu->flags.C = ((u16)cpu->regs[REG_A] & 0xFF) + ((u16)op & 0xFF) > 0xFF;
		cpu->regs[REG_A] += op;
		cpu->flags.Z = cpu->regs[REG_A] == 0;
	}
}

static void AND(struct cpu* cpu, struct instruction* instr) {
	i8 op = cpu_get_operand(cpu, instr->op2);
	cpu->regs[REG_A] &= op;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
	cpu->flags.N = false;
	cpu->flags.H = true;
	cpu->flags.C = false;
}

static void BIT(struct cpu* cpu, struct instruction* instr) {
	u16 b = cpu_get_operand(cpu, instr->op1) & 0x0FF;
	i8 op = cpu_get_operand(cpu, instr->op2);
	cpu->flags.Z = ((op >> b) & 1) == 0;
	cpu->flags.N = false;
	cpu->flags.H = true;
}

static void CALL(struct cpu* cpu, struct instruction* instr) {
	u16 addr = cpu_get_operand(cpu, instr->op2);
	bool cond = cpu_check_cond(cpu, instr->op1);
	if (cond) {
		cpu->SP -= 2;
		mem_write16(cpu->mem, cpu->SP, cpu->PC);
		cpu->PC = addr;
	}
	else
		cpu->cycles_left -= (instr->cycles - instr->cycles_alt);
}

static void CCF(struct cpu* cpu, struct instruction* instr) {
	cpu->flags.N = false;
	cpu->flags.H = false;
	cpu->flags.C = !cpu->flags.C;
}

static void CP(struct cpu* cpu, struct instruction* instr) {
	i8 op = cpu_get_operand(cpu, instr->op2);
	cpu->flags.N = true;
	cpu->flags.H = (cpu->regs[REG_A] & 0x0F) < (op & 0x0F);
	cpu->flags.C = ((u16)cpu->regs[REG_A] & 0xFF) < ((u16)op & 0xFF);
	cpu->flags.Z = cpu->regs[REG_A] == op;
}

static void CPL(struct cpu* cpu, struct instruction* instr) {
	cpu->flags.N = true;
	cpu->flags.H = true;
	cpu->regs[REG_A] = ~cpu->regs[REG_A];
}

static void DAA(struct cpu* cpu, struct instruction* instr) {
	// see: https://blog.ollien.com/posts/gb-daa/
    i8 offset = 0;
    u16 a = cpu->regs[REG_A] & 0x0FF; // force unsigned for cmp to 0x99
    bool carry_out = false;
    if ( (!cpu->flags.N && (a & 0x0F) > 0x09) || cpu->flags.H) {
        offset |= 0x06;
    }
    if ( (!cpu->flags.N && a > 0x99) || cpu->flags.C) {
        offset |= 0x60;
		carry_out = true;
    }
	a = cpu->flags.N ? a - offset : a + offset;
	cpu->regs[REG_A] = a & 0xff;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
	cpu->flags.H = false;
	cpu->flags.C = carry_out;
}

static void DEC(struct cpu* cpu, struct instruction* instr) {
	if (cpu_get_operand_size(cpu, instr->op1) == 16) {
		cpu_set_operand(cpu, instr->op1, cpu_get_operand(cpu, instr->op1) - 1);
	}
	else {
		i8 op = cpu_get_operand(cpu, instr->op1);
		cpu->flags.H = (op & 0x0F) == 0;
		cpu->flags.N = true;
		--op;
		cpu->flags.Z = op == 0;
		cpu_set_operand(cpu, instr->op1, op);
	}
}

static void DI(struct cpu* cpu, struct instruction* instr) {
	cpu->ime = false;
}

static void EI(struct cpu* cpu, struct instruction* instr) {
	cpu->ei_initiated = true;
}

static void HALT(struct cpu* cpu, struct instruction* instr) {
	cpu->halted = true;
 	 // HALT called when interrupts pending: read next byte @ PC twice:
	cpu->haltbug = !cpu->ime && mem_get_active_interrupts(cpu->mem) != 0;
}

static void INC(struct cpu* cpu, struct instruction* instr) {
	if (cpu_get_operand_size(cpu, instr->op1) == 16) {
		cpu_set_operand(cpu, instr->op1, cpu_get_operand(cpu, instr->op1) + 1);
	}
	else {
		i8 op = cpu_get_operand(cpu, instr->op1);
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
	cpu->cycles_left -= cond ? 0 : (instr->cycles - instr->cycles_alt);
}

static void JR(struct cpu* cpu, struct instruction* instr) {
	i8 offs = cpu_get_operand(cpu, instr->op2);
	bool cond = cpu_check_cond(cpu, instr->op1);
	cpu->PC = cond ? cpu->PC + offs : cpu->PC;
	cpu->cycles_left -= cond ? 0 : (instr->cycles - instr->cycles_alt);
}

static void LD(struct cpu* cpu, struct instruction* instr) {
	// Special case: LD HL, SP + imm8
	if (instr->op2 == SP_IMM8) {
		i8 imm8 = cpu_get_operand(cpu, instr->op2);
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
}

static void OR(struct cpu* cpu, struct instruction* instr) {
	i8 op = cpu_get_operand(cpu, instr->op2);
	cpu->regs[REG_A] |= op;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
	cpu->flags.N = false;
	cpu->flags.H = false;
	cpu->flags.C = false;
}

static void POP(struct cpu* cpu, struct instruction* instr) {
	u16 w = mem_read16(cpu->mem, cpu->SP);
	cpu->SP += 2;
	cpu_set_operand(cpu, instr->op1, w);
}

static void PUSH(struct cpu* cpu, struct instruction* instr) {
	u16 w = cpu_get_operand(cpu, instr->op1);
	cpu->SP -= 2;
	mem_write16(cpu->mem, cpu->SP, w);
}

static void RES(struct cpu* cpu, struct instruction* instr) {
	u16 b = cpu_get_operand(cpu, instr->op1);
	i8 op = cpu_get_operand(cpu, instr->op2);
	op &= ~(1 << b);
	cpu_set_operand(cpu, instr->op2, op);
}

static void RET(struct cpu* cpu, struct instruction* instr) {
	bool cond = cpu_check_cond(cpu, instr->op1);
	if (cond) {
		cpu->PC = mem_read16(cpu->mem, cpu->SP);
		cpu->SP += 2;
	}
	else
		cpu->cycles_left -= (instr->cycles - instr->cycles_alt);
}

static void RETI(struct cpu* cpu, struct instruction* instr) {
	cpu->ime = true;
	cpu->PC = mem_read16(cpu->mem, cpu->SP);
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
    u16 a = cpu->regs[REG_A] & 0x0FF; // expand width
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
    u16 a = cpu->regs[REG_A] & 0x0FF; // expand width
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
    u16 a = cpu->regs[REG_A] & 0x0FF; // expand width
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
    u16 a = cpu->regs[REG_A] & 0x0FF; // expand width
	cpu->flags.C = (a & 1) == 1;
	cpu->regs[REG_A] = (a >> 1) | (cpu->flags.C ? 0x80 : 0);
	cpu->flags.Z = false;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void RST(struct cpu* cpu, struct instruction* instr) {
	cpu->SP -= 2;
	mem_write16(cpu->mem, cpu->SP, cpu->PC);
	cpu->PC = 8 * cpu_get_operand(cpu, instr->op1);
}

static void SBC(struct cpu* cpu, struct instruction* instr) {
	i8 op = cpu_get_operand(cpu, instr->op2);
	i8 c = cpu->flags.C ? 1 : 0;
	cpu->flags.N = true;
	cpu->flags.H = (cpu->regs[REG_A] & 0x0F) < (op & 0x0F) + c;
	cpu->flags.C = ((u16)cpu->regs[REG_A] & 0xFF) < ((u16)op & 0xFF) + c;
	cpu->regs[REG_A] -= op + c;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
}

static void SCF(struct cpu* cpu, struct instruction* instr) {
	cpu->flags.N = false;
	cpu->flags.H = false;
	cpu->flags.C = true;
}

static void SET(struct cpu* cpu, struct instruction* instr) {
	u16 b = cpu_get_operand(cpu, instr->op1);
	i8 op = cpu_get_operand(cpu, instr->op2);
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
	printf("CPU: STOP instr at %04X\n", cpu->PC - 1);
	cpu->stopped = true;
}

static void SUB(struct cpu* cpu, struct instruction* instr) {
	i8 op = cpu_get_operand(cpu, instr->op2);
	cpu->flags.N = true;
	cpu->flags.H = (cpu->regs[REG_A] & 0x0F) < (op & 0x0F);
	cpu->flags.C = ((u16)cpu->regs[REG_A] & 0xFF) < ((u16)op & 0xFF);
	cpu->regs[REG_A] -= op;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
}

static void SWAP(struct cpu* cpu, struct instruction* instr) {
    u16 r = cpu_get_operand(cpu, instr->op1) & 0x0FF;
	r = (r >> 4) | ((r & 0x0F) << 4);
	cpu_set_operand(cpu, instr->op1, r);
	cpu->flags.Z = r == 0;
	cpu->flags.C = false;
	cpu->flags.N = false;
	cpu->flags.H = false;
}

static void XOR(struct cpu* cpu, struct instruction* instr) {
	i8 op = cpu_get_operand(cpu, instr->op2);
	cpu->regs[REG_A] ^= op;
	cpu->flags.Z = cpu->regs[REG_A] == 0;
	cpu->flags.N = false;
	cpu->flags.H = false;
	cpu->flags.C = false;
}

static void PREFIX(struct cpu* cpu, struct instruction* instr) {printf("%s should not have been called\n", instr->mnemonic);}

static void ILLEGAL(struct cpu* cpu, struct instruction* instr) {printf("%s not implemented yet\n", instr->mnemonic);}


#include "opcode_table.inc"
