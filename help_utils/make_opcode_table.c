#include <stdbool.h>
#include <stdio.h>
#include <string.h>

const char* r8_to_str[] = { "REG_B", "REG_C", "REG_D", "REG_E", "REG_H", "REG_L", "MEM_HL", "REG_A" };
const char* r16_to_str[] = { "REG_BC", "REG_DE", "REG_HL", "REG_SP" };
const char* r16stk_to_str[] = { "REG_BC", "REG_DE", "REG_HL", "REG_AF" };
const char* m16_to_str[] = { "MEM_BC", "MEM_DE", "MEM_HLI", "MEM_HLD" };
const char* cc_to_str[] = { "COND_NZ", "COND_Z", "COND_NC", "COND_C", "COND_NIL" };
const char* literal_to_str[] = {"LIT0", "LIT1", "LIT2", "LIT3", "LIT4", "LIT5", "LIT6", "LIT7" };

#define NIL   "NIL"
#define IMM8  "IMM8"
#define IMM16 "IMM16"
#define MEM_IMM8  "MEM_IMM8"
#define MEM_IMM16 "MEM_IMM16"
#define MEM16B_IMM16 "MEM16B_IMM16" /* for LD [a16], SP only */
#define SP_PLUS_IMM8 "SP_IMM8"
#define MEM_C "MEM_C"

void println(char* mnemonic, const char* op1, const char* op2, int cycles, int cycles2, bool legal, int opcode) {
	printf("\t{%*s", 8 - (int)strlen(mnemonic), "");
	//printf("\"%s\", %7s, %9s, %9s, %2d, %2d, %6s},", mnemonic, mnemonic, op1, op2, cycles, cycles2, legal?"true":"false");
	printf("\"%s\", %7s, %12s, %9s, %2d, %2d},", mnemonic, mnemonic, op1, op2, cycles, cycles2);
	printf(" // opcode 0x%02X = OCT %03o\n", opcode, opcode);
}

void block0(int code) {
	int p0 = code & 0x07;
	int p1 = (code >> 3) & 0x07;
	switch (p0) {
		case 0:
			if (p1 == 0)
				println("NOP", NIL, NIL, 1, 0, true, code);
			else if (p1 == 1)
				println("LD", MEM16B_IMM16, r16_to_str[3], 5, 0, true, code);
			else if (p1 == 2)
				println("STOP", NIL, NIL, 1, 0, true, code);
			else if (p1 == 3)
				println("JR", cc_to_str[4], IMM8, 3, 2, true, code);
			else
				println("JR", cc_to_str[p1 - 4], IMM8, 3, 2, true, code);
			break;
		case 1:
			if ((p1 & 1) == 0)
				println("LD", r16_to_str[p1 >> 1], IMM16, 3, 0, true, code);
			else
				println("ADD", r16_to_str[2], r16_to_str[p1 >> 1], 2, 0, true, code);
			break;
		case 2:
			if ((p1 & 1) == 0)
				println("LD", m16_to_str[p1 >> 1], r8_to_str[7], 2, 0, true, code);
			else
				println("LD", r8_to_str[7], m16_to_str[p1 >> 1], 2, 0, true, code);
			break;
		case 3:
			if ((p1 & 1) == 0)
				println("INC", r16_to_str[p1 >> 1], NIL, 2, 0, true, code);
			else
				println("DEC", r16_to_str[p1 >> 1], NIL, 2, 0, true, code);
			break;
		case 4:
			println("INC", r8_to_str[p1], NIL, p1 == 6 ? 3 : 1, 0, true, code);
			break;
		case 5:
			println("DEC", r8_to_str[p1], NIL, p1 == 6 ? 3 : 1, 0, true, code);
			break;
		case 6:
			println("LD", r8_to_str[p1], IMM8, p1 == 6 ? 3 : 2, 0, true, code);
			break;
		case 7:
			{
				char* mnems[] = {"RLCA","RRCA","RLA","RRA","DAA","CPL","SCF","CCF"};
				println(mnems[p1], NIL, NIL, 1, 0, true, code);
			}
			break;
	}
}

void block1(int code) {
	int p0 = code & 0x07;
	int p1 = (code >> 3) & 0x07;
	if (p0 == 6 && p1 == 6)
		println("HALT", NIL, NIL, 1, 0, true, code | (1 << 6));
	else
		println("LD", r8_to_str[p1], r8_to_str[p0], (p1 == 6 || p0 == 6) ? 2 : 1, 0, true, code | (1 << 6));
}

void block2(int code) {
	int p0 = code & 0x07;
	int p1 = (code >> 3) & 0x07;
	char* mnems[] = {"ADD","ADC","SUB","SBC","AND","XOR","OR","CP"};
	println(mnems[p1], r8_to_str[7], r8_to_str[p0], p0 == 6 ? 2 : 1, 0, true, code | (2 << 6));
}

void block3(int code) {
	int p0 = code & 0x07;
	int p1 = (code >> 3) & 0x07;
	switch (p0) {
		case 0:
			if (p1 < 4)
				println("RET", cc_to_str[p1], NIL, 5, 2, true, code | (3 << 6));
			else {
				if (p1 == 4)
					println("LDH", MEM_IMM8, r8_to_str[7], 3, 0, true, code | (3 << 6));
				else if (p1 == 5)
					println("ADD", r16_to_str[3], IMM8, 4, 0, true, code | (3 << 6));
				else if (p1 == 6)
					println("LDH", r8_to_str[7], MEM_IMM8, 3, 0, true, code | (3 << 6));
				else
					println("LD", r16_to_str[2], SP_PLUS_IMM8, 3, 0, true, code | (3 << 6));
			}
			break;
		case 1:
			if ((p1 & 1) == 0)
				println("POP", r16stk_to_str[p1 >> 1], NIL, 3, 0, true, code | (3 << 6));
			else if (p1 == 1)
				println("RET", cc_to_str[4], NIL, 4, 2, true, code | (3 << 6));
			else if (p1 == 3)
				println("RETI", cc_to_str[4], NIL, 4, 2, true, code | (3 << 6));
			else if (p1 == 5)
				println("JP", cc_to_str[4], r16_to_str[2], 1, 1, true, code | (3 << 6));
			else
				println("LD", r16_to_str[3], r16_to_str[2], 2, 0, true, code | (3 << 6));
			break;
		case 2:
			if (p1 < 4)
				println("JP", cc_to_str[p1], IMM16, 4, 3, true, code | (3 << 6));
			else if (p1 == 4)
				println("LDH", MEM_C, r8_to_str[7], 2, 0, true, code | (3 << 6));
			else if (p1 == 5)
				println("LD", MEM_IMM16, r8_to_str[7], 4, 0, true, code | (3 << 6));
			else if (p1 == 6)
				println("LDH", r8_to_str[7], MEM_C, 2, 0, true, code | (3 << 6));
			else
				println("LD", r8_to_str[7], MEM_IMM16, 4, 0, true, code | (3 << 6));
			break;
		case 3:
			if (p1 == 0)
				println("JP", cc_to_str[4], IMM16, 4, 3, true, code | (3 << 6));
			else if (p1 == 1)
				println("PREFIX", NIL, NIL, 1, 0, true, code | (3 << 6)); // Cycles not used in emulator
			else if (p1 == 6)
				println("DI", NIL, NIL, 1, 0, true, code | (3 << 6));
			else if (p1 == 7)
				println("EI", NIL, NIL, 1, 0, true, code | (3 << 6));
			else
				println("ILLEGAL", NIL, NIL, 1, 0, false, code | (3 << 6));
			break;
		case 4:
			if (p1 < 4)
				println("CALL", cc_to_str[p1], IMM16, 6, 3, true, code | (3 << 6));
			else
				println("ILLEGAL", NIL, NIL, 1, 0, false, code | (3 << 6));
			break;
		case 5:
			if ((p1 & 1) == 0)
				println("PUSH", r16stk_to_str[p1 >> 1], NIL, 4, 0, true, code | (3 << 6));
			else if (p1 == 1)
				println("CALL", cc_to_str[4], IMM16, 6, 3, true, code | (3 << 6));
			else
				println("ILLEGAL", NIL, NIL, 1, 0, false, code | (3 << 6));
			break;
		case 6:
			{
				char* mnems[] = {"ADD","ADC","SUB","SBC","AND","XOR","OR","CP"};
				println(mnems[p1], r8_to_str[7], IMM8, 2, 0, true, code | (3 << 6));
			}
			break;
		case 7:
			println("RST", literal_to_str[p1], NIL, 4, 0, true, code | (3 << 6));
			break;
	}
}

void prefix_block0(int code) {
	int p0 = code & 0x07;
	int p1 = (code >> 3) & 0x07;
	char* mnems[] = {"RLC","RRC","RL","RR","SLA","SRA","SWAP","SRL"};
	println(mnems[p1], r8_to_str[p0], NIL, p0 == 6 ? 3 : 1, 0, true, code);
}

void prefix_block1(int code) {
	int p0 = code & 0x07;
	int p1 = (code >> 3) & 0x07;
	println("BIT", literal_to_str[p1], r8_to_str[p0], p0 == 6 ? 2 : 1, 0, true, code | (1 << 6));
}

void prefix_block2(int code) {
	int p0 = code & 0x07;
	int p1 = (code >> 3) & 0x07;
	println("RES", literal_to_str[p1], r8_to_str[p0], p0 == 6 ? 3 : 1, 0, true, code | (2 << 6));
}

void prefix_block3(int code) {
	int p0 = code & 0x07;
	int p1 = (code >> 3) & 0x07;
	println("SET", literal_to_str[p1], r8_to_str[p0], p0 == 6 ? 3 : 1, 0, true, code | (3 << 6));
}

int main() {
	printf("static struct instruction opcode_lookup[] = {\n");
	//printf("\t// mnemonic, function, op1, op2, cycles, cycles_alt, legal\n");
	printf("\t// mnemonic, function, op1, op2, cycles, cycles_alt\n");
	// print all opcodes
	for (int opcode = 0; opcode <= 0xFF; ++opcode) {
		int block = opcode >> 6;
		if (block == 0)
			block0(opcode & 0x3f);
		else if (block == 1)
			block1(opcode & 0x3f);
		else if (block == 2)
			block2(opcode & 0x3f);
		else // if (block == 3)
			block3(opcode & 0x3f);
	}
	printf("// PREFIX (0xCB) opcodes:\n");
	// print PREFIX (0xCB) instructions
	for (int opcode = 0; opcode <= 0xFF; ++opcode) {
		int block = opcode >> 6;
		if (block == 0)
			prefix_block0(opcode & 0x3f);
		else if (block == 1)
			prefix_block1(opcode & 0x3f);
		else if (block == 2)
			prefix_block2(opcode & 0x3f);
		else // if (block == 3)
			prefix_block3(opcode & 0x3f);
	}
	printf("};\n");
	return 0;
}
