#ifndef BITS_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

struct bits {
	uint8_t* bd;
	long n;
};

static uint8_t* bits__init0_from_bytes(struct bits* bits, const uint8_t* src_bytes, long n_bytes)
{
	memset(bits, 0, sizeof *bits);
	const int n_bits = bits->n = 8*n_bytes;
	bits->bd = malloc(n_bits + n_bytes);
	uint8_t* bytes = bits->bd + n_bits;
	memcpy(bytes, src_bytes, n_bytes);
	return bytes;
}

static uint8_t* bits__init0_from_path(struct bits* bits, const char* path)
{
	memset(bits, 0, sizeof *bits);
	FILE* f = fopen(path, "rb");
	assert(f && "file not found");
	assert(fseek(f, 0, SEEK_END) == 0);
	const long n_bytes = ftell(f);
	const int n_bits = bits->n = 8*n_bytes;
	bits->bd = malloc(n_bits + n_bytes);
	uint8_t* bytes = bits->bd + n_bits;
	assert(fseek(f, 0, SEEK_SET) == 0);
	assert(fread(bytes, n_bytes, 1, f) == 1);
	return bytes;
}

static void bits__init1_populate_bits_lsb_first(struct bits* bits, uint8_t* bytes)
{
	uint8_t* p = bits->bd;
	for (int i = 0; i < bits->n; i++) *(p++) = (bytes[i >> 3] & (1 << (i&7))) != 0;
}

static void bits__init1_populate_bits_msb_first(struct bits* bits, uint8_t* bytes)
{
	uint8_t* p = bits->bd;
	for (int i = 0; i < bits->n; i++) *(p++) = (bytes[i >> 3] & (1 << ((7-i)&7))) != 0;
}

static struct bits bits_load_lsb_first(const char* path)
{
	struct bits bits;
	bits__init1_populate_bits_lsb_first(&bits, bits__init0_from_path(&bits, path));
	return bits;
}

static struct bits bits_load_msb_first(const char* path)
{
	struct bits bits;
	bits__init1_populate_bits_msb_first(&bits, bits__init0_from_path(&bits, path));
	return bits;
}

static struct bits bits_slice(struct bits bits, int offset, int length)
{
	struct bits r;
	assert(offset >= 0);
	assert(length >= -1);
	r.bd = bits.bd + offset;
	int max_length = bits.n - offset;
	if (max_length < 0) max_length = 0;
	if (length == -1) {
		r.n = max_length;
	} else {
		r.n = length > max_length ? max_length : length;
	}
	return r;
}

static int bits_match_prefix_ascii(struct bits bits, const char* bit_string)
{
	const int len = strlen(bit_string);
	if (bits.n < len) return 0;
	for (int i = 0; i < len; i++) {
		char ch      = bit_string[i];
		const int bi = bits.bd[i];
		if (ch == '0') {
			if (bi != 0) return 0;
		} else if (ch == '1') {
			if (bi != 1) return 0;
		} else {
			assert(!"invalid bit string character");
		}
	}
	return 1;
}

static uint8_t* bits_extract_bytes_msb_first(struct bits bits, int n_bytes)
{
	const int n_bits = 8*n_bytes;
	assert(n_bits <= bits.n);
	uint8_t* bs = malloc(n_bytes);
	uint8_t* bp = bs;
	for (int i0 = 0; i0 < n_bits; i0++) {
		uint8_t b = 0;
		for (int i1 = 0; i1 < 8; i1++) b |= (bits.bd[i0] ? (1 << (7-i1)) : 0);
		*(bp++) = b;
	}
	return bs;
}

static uint8_t* bits_extract_bytes_lsb_first(struct bits bits, int n_bytes)
{
	const int n_bits = 8*n_bytes;
	assert(n_bits <= bits.n);
	uint8_t* bs = malloc(n_bytes);
	uint8_t* bp = bs;
	for (int i0 = 0; i0 < n_bits; i0++) {
		uint8_t b = 0;
		for (int i1 = 0; i1 < 8; i1++) b |= (bits.bd[i0] ? (1 << i1) : 0);
		*(bp++) = b;
	}
	return bs;
}

static void bits_dump(struct bits bits)
{
	for (int i = 0; i < bits.n; i++) printf("%c", bits.bd[i] ? '1' : '0');
}

static struct bits bits_dup(struct bits src)
{
	struct bits dst;
	dst.n = src.n;
	dst.bd = malloc(src.n);
	memcpy(dst.bd, src.bd, src.n);
	return dst;
}

static int bits_crc16(struct bits bits)
{
	unsigned crc = 0;
	int remaining = bits.n;
	uint8_t* bp = bits.bd;
	while (remaining > 0) {
		int incoming_bit = *(bp++);
		crc <<= 1;
		int b16 = (crc >> 16) != 0;
		if (b16 ^ incoming_bit) crc ^= (1 + (1<<5) + (1<<12)); // =0x1021
		crc &= 0xffff;
		remaining--;
	}
	return crc;
}

static uint16_t bits_u16(struct bits bits)
{
	uint16_t v = 0;
	int n = 16;
	if (n > bits.n) n = bits.n;
	//for (int i = 0; i < n; i++) if (bits.bd[i]) v |= 1 << (n-1-i);
	for (int i = 0; i < n; i++) if (bits.bd[i]) v |= 1 << i;
	return v;
}

static uint8_t bits_u8(struct bits bits)
{
	uint8_t v = 0;
	int n = 8;
	if (n > bits.n) n = bits.n;
	//for (int i = 0; i < n; i++) if (bits.bd[i]) v |= 1 << (n-1-i);
	for (int i = 0; i < n; i++) if (bits.bd[i]) v |= 1 << i;
	return v;
}

static int bits_popcnt(struct bits bits)
{
	int popcnt = 0;
	for (int i = 0; i < bits.n; i++) if (bits.bd[i]) popcnt++;
	return popcnt;
}

static struct bits bits_invert(struct bits bits)
{
	struct bits r;
	r.n = bits.n;
	r.bd = malloc(r.n);
	for (int i = 0; i < r.n; i++) r.bd[i] = !bits.bd[i];
	return r;
}

static struct bits bits_vote(struct bits* bss, int nbss)
{
	assert(nbss > 0);
	long min_length = 0;
	for (int i = 0; i < nbss; i++) {
		int n = bss[i].n;
		if (i == 0 | n < min_length) min_length = n;
	}
	assert(min_length > 0);

	struct bits r;
	r.n = min_length;
	r.bd = malloc(r.n);
	for (int i0 = 0; i0 < min_length; i0++) {
		int zero_votes = 0;
		int one_votes = 0;
		for (int i1 = 0; i1 < nbss; i1++) {
			if (bss[i1].bd[i0]) {
				one_votes++;
			} else {
				zero_votes++;
			}
		}
		r.bd[i0] = one_votes > zero_votes;
	}

	return r;
}

#define BITS_H
#endif

#ifdef UNIT_TEST
// cc -DUNIT_TEST -xc bits.h -o test_bits && ./test_bits

int FAIL = 0;

static void test_crc16(const uint8_t* input, int n, uint16_t expected_crc)
{
	struct bits bits;
	bits__init1_populate_bits_msb_first(&bits, bits__init0_from_bytes(&bits, (const uint8_t*)input, n));
	uint16_t actual_crc = bits_crc16(bits);
	if (actual_crc != expected_crc) {
		fprintf(stderr, "CRC16 of \"%s\" was 0x%.4x, but 0x%.4x was expected\n", input, actual_crc, expected_crc);
		FAIL = 1;
	}
}

int main()
{
	test_crc16("1",                1,   0x2672);
	test_crc16("1\x26\x72",        1+2, 0);

	test_crc16("12",               2,   0x20b5);
	test_crc16("12\x20\xb5",       2+2, 0);

	test_crc16("123",              3,   0x9752);
	test_crc16("123\x97\x52",      3+2, 0);

	test_crc16("1234",             4,   0xd789);
	test_crc16("1234\xd7\x89",     4+2, 0);

	test_crc16("12345",            5,   0x546c);
	test_crc16("12345\x54\x6c",    5+2, 0);

	test_crc16("123456",           6,   0x20e4);
	test_crc16("123456\x20\xe4",   6+2, 0);

	test_crc16("1234567",          7,   0x86d6);
	test_crc16("1234567\x86\xd6",  7+2, 0);

	test_crc16("12345678",         8,   0x9015);
	test_crc16("12345678\x90\x15", 8+2, 0);

	test_crc16("\00012345678\x90\x15", 1+8+2, 0);
	test_crc16("\000\00012345678\x90\x15", 2+8+2, 0);

	test_crc16("\x00",     1, 0);
	test_crc16("\x00\x00", 2, 0);

	if (!FAIL) printf("OK!\n");

	return FAIL ? EXIT_FAILURE : EXIT_SUCCESS;
}

#endif
