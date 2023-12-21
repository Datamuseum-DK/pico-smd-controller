// make bitdump && ./bitdump <path/to/file> [n bits]

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "Usage: %s <path/to/file.nrz> [n bits]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	const int n_bits = argc == 3 ? atoi(argv[2]) : -1;

	const char* path = argv[1];
	FILE* f = fopen(path, "rb");
	if (f == NULL) {
		fprintf(stderr, "%s: could not open\n", path);
		exit(EXIT_FAILURE);
	}

	assert(fseek(f, 0, SEEK_END) == 0);
	long sz = ftell(f);
	assert(fseek(f, 0, SEEK_SET) == 0);

	uint8_t* data = malloc(sz);
	assert(fread(data, sz, 1, f) == 1);

	assert(fclose(f) == 0);

	uint8_t* p = data;
	for (int bit = 0; (n_bits == -1 || bit < n_bits) && bit < (sz<<3); bit += 8) {
		uint8_t x = *(p++);
		//for (int b = 0; b < 8; b++) fputc(((x >> (7-b)) & 1) ? '1' : '0', stdout);
		for (int b = 0; b < 8; b++) fputc(((x >> (b)) & 1) ? '@' : '.', stdout);
		//if (((bit >> 3) % 20) == 19) fputc('\n', stdout);
	}
	printf("\n");

	return EXIT_SUCCESS;
}
