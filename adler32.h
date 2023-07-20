#ifndef ADLER32_H

#include <stdint.h>
#include <stddef.h>

struct adler32 {
	uint32_t a, b;
};

uint32_t adler32(const uint8_t* data, size_t n);
void adler32_init(struct adler32* adler);
void adler32_push(struct adler32*, const uint8_t* data, size_t n);
uint32_t adler32_sum(struct adler32*);

#define ADLER32_H
#endif
