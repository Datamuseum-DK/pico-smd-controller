// make loopback_verify && ./loopback_verify <path/to/file>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loopback_test_generate_data.h"

int main(int argc, char** argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <path/to/file.nrz>\n", argv[0]);
		fprintf(stderr, "Verifies a \"Loopback Test\" data download\n");
		exit(EXIT_FAILURE);
	}

	const char* path = argv[1];
	FILE* f = fopen(path, "rb");
	if (f == NULL) {
		fprintf(stderr, "%s: could not open\n", path);
		exit(EXIT_FAILURE);
	}

	assert(fseek(f, 0, SEEK_END) == 0);
	long sz = ftell(f);
	assert(fseek(f, 0, SEEK_SET) == 0);

	uint8_t* actual = malloc(sz);
	assert(fread(actual, sz, 1, f) == 1);

	uint8_t* expected = malloc(sz);
	loopback_test_generate_data(expected, sz);

	int err = 0;
	int first_error_index = -1;
	int last_error_index = -1;
	for (long i = 0; i < sz; i++) {
		if (actual[i] != expected[i]) {
			if (err == 0) first_error_index = i;
			last_error_index = i;
			err = 1;
		}
	}

	if (err) {
		fprintf(stderr, "%s: ERRORS between offset %d (0x%x) and %d (0x%x)\n", path, first_error_index, first_error_index, last_error_index, last_error_index);
	} else {
		printf("%s: OK!\n", path);
	}

	assert(fclose(f) == 0);
	return EXIT_SUCCESS;
}
