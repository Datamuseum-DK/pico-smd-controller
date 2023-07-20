#include "adler32.h"

#include <string.h>

void adler32_init(struct adler32* adler)
{
	memset(adler, 0, sizeof *adler);
	adler->a = 1;
}

void adler32_push(struct adler32* adler, const uint8_t* data, size_t n)
{
	const uint8_t* rp = data;
	size_t remaining = n;
	while (remaining > 0) {
		const size_t nrun_max = 256;
		size_t nrun = remaining > nrun_max ? nrun_max : remaining;
		const uint32_t mod = 65521;
		for (size_t i = 0; i < nrun; i++) {
			adler->a += *(rp++);
			adler->b += adler->a;
		}
		adler->a %= mod;
		adler->b %= mod;
		remaining -= nrun;
	}
}

uint32_t adler32_sum(struct adler32* adler)
{
	return (adler->b<<16) | adler->a;
}

uint32_t adler32(const uint8_t* data, size_t n)
{
	struct adler32 adler;
	adler32_init(&adler);
	adler32_push(&adler, data, n);
	return adler32_sum(&adler);
}

// -----------------------------------------------------------------------------------------
// cc -DUNIT_TEST adler32.c -o unittest_adler32 && ./unittest_adler32
#ifdef UNIT_TEST

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

int FAIL = 0;

static void test0(const char* str, uint32_t expected_checksum)
{
	const uint32_t actual_checksum = adler32(str, strlen(str));
	if (actual_checksum != expected_checksum) {
		fprintf(stderr, "FAIL: expected adler32 of \"%s\" to checksum to %d, but got %d\n", str, expected_checksum, actual_checksum);
		FAIL = 1;
	}
}

static void test1(char ch, int n, uint32_t expected_checksum)
{
	char* bs = malloc(n);
	for (int i = 0; i < n; i++) bs[i] = ch;
	const uint32_t actual_checksum = adler32(bs, n);
	free(bs);
	if (actual_checksum != expected_checksum) {
		fprintf(stderr, "FAIL: expected adler32 of %d×'%c' to checksum to %d, but got %d\n", n, ch, expected_checksum, actual_checksum);
		FAIL = 1;
	}
}

int main(int argc, char** argv)
{
	// testing against zlib.adler32() in Python
	test0(".", 3080239);
	test0("xyzwwyzwzzzz31232132131", 1880754130);
	test0("af.aewf.32r.h.y.hjfgkdsjfjakhk43htkh5h6hkj45h6kj54h6kj45h6k456ds.f.hgfh;'h;'t;h;';hgfhgf;h;ffhg", 39722923);
	test1('?', 1000000, 677086736);
	test1('~', 1000000, 227871790);
	test1(255, 1000000, 943972798);
	test1(254, 10000000, 1249063539);
	printf("OK\n");
	return FAIL ? EXIT_FAILURE : EXIT_SUCCESS;
}

#endif
