#include "base.h"
#include "b64.h"

#define BASE64_DIGITS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
//                     0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF

static inline char get_base64_digit(unsigned v)
{
	if (v >= 64) PANIC(PANIC_XXX);
	return (BASE64_DIGITS)[v];
}

char* b64_enc(char* output, uint8_t* input, int n_bytes)
{
	uint8_t* rp = input;
	char* wp = output;
	int n_bytes_remaining = n_bytes;
	while (n_bytes_remaining >= 3) {
		uint8_t b0 = *(rp)++;
		uint8_t b1 = *(rp)++;
		uint8_t b2 = *(rp)++;
		*(wp++) = get_base64_digit((b0 >> 2) & 0x3f);
		*(wp++) = get_base64_digit(((b0 & 0x3) << 4) | ((b1 >> 4) & 0xf));
		*(wp++) = get_base64_digit(((b1 & 0xf) << 2) | ((b2 >> 6) & 0x3));
		*(wp++) = get_base64_digit(b2 & 0x3f);
		n_bytes_remaining -= 3;
	}
	switch (n_bytes_remaining) {
	case 0: break;
	case 1: {
		uint8_t b0 = *(rp)++;
		*(wp++) = get_base64_digit((b0 >> 2) & 0x3f);
		*(wp++) = get_base64_digit((b0 & 0x3) << 4);
		*(wp++) = '=';
		*(wp++) = '=';
	} break;
	case 2: {
		uint8_t b0 = *(rp)++;
		uint8_t b1 = *(rp)++;
		*(wp++) = get_base64_digit((b0 >> 2) & 0x3f);
		*(wp++) = get_base64_digit(((b0 & 0x3) << 4) | ((b1 >> 4) & 0xf));
		*(wp++) = get_base64_digit((b1 & 0xf) << 2);
		*(wp++) = '=';
	} break;
	default: PANIC(PANIC_XXX);
	}
	return wp;
}

// -----------------------------------------------------------------------------------------
// cc -DUNIT_TEST b64.c -o unittest_b64 && ./unittest_b64
#ifdef UNIT_TEST

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

void enc(char* output, uint8_t* input, int n_bytes)
{
	char* w = b64_enc(output, input, n_bytes);
	*(w++) = 0;
	#if 0
	printf("enc -> [%s]\n", output);
	#endif
}

int main(int argc, char** argv)
{
	uint8_t xs[] = {
		0x10, 0x20, 0x30,
		0x60, 0x70, 0x90,
		0xB0, 0xC0, 0xD0,
		0xFF, 0xEE, 0xDD,
		0x01, 0x02, 0x03,
		0x3c, 0x3b, 0x3a,
		0x77, 0x66, 0x55,
	};
	char line[1<<10];

	enc(line, xs, ARRAY_LENGTH(xs));
	assert(strcmp(line, "ECAwYHCQsMDQ/+7dAQIDPDs6d2ZV") == 0 && "b64_enc() failed");

	enc(line, xs, ARRAY_LENGTH(xs)-1);
	assert(strcmp(line, "ECAwYHCQsMDQ/+7dAQIDPDs6d2Y=") == 0 && "b64_enc() failed");

	enc(line, xs, ARRAY_LENGTH(xs)-2);
	assert(strcmp(line, "ECAwYHCQsMDQ/+7dAQIDPDs6dw==") == 0 && "b64_enc() failed");

	enc(line, xs, ARRAY_LENGTH(xs)-3);
	assert(strcmp(line, "ECAwYHCQsMDQ/+7dAQIDPDs6") == 0 && "b64_enc() failed");

	printf("OK\n");
	return EXIT_SUCCESS;
}

void PANIC(uint32_t error)
{
	fprintf(stderr, "PANIC! (CODE 0x%X)\n", error);
	abort();
}

#endif
