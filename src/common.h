#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdint.h>

typedef int8_t   i8;   // signed byte
typedef uint8_t  u8;   // unsigned byte
typedef uint16_t u16;  // unsigned word

typedef uint8_t gb_color_idx; // 2 bit color index (before applying palette)
typedef uint8_t gb_color;     // 2 bit color (after applying palette)

struct limeguy_color {
	u8 r;
	u8 g;
	u8 b;
	u8 a;
};

#endif
