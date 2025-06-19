// cc bits2png.c -o bits2png
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char** argv)
{
	if (argc != 4 && argc != 5) {
		fprintf(stderr, "Usage: %s <path/to/bits> <path/to/out.png> <stride> [head]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	const char* bits_path = argv[1];
	const char* png_path = argv[2];
	const int stride = atoi(argv[3]);
	if (stride <= 0) {
		fprintf(stderr, "invalid stride (%d)\n", stride);
		exit(EXIT_FAILURE);
	}

	const int head = (argc==5) ? atoi(argv[4]) : -1;

	FILE* f = fopen(bits_path, "rb");
	if (f == NULL) {
		fprintf(stderr, "%s: could not open\n", bits_path);
		exit(EXIT_FAILURE);
	}

	assert(fseek(f, 0, SEEK_END) == 0);
	const long filesize = ftell(f);
	if (filesize == 0) {
		fprintf(stderr, "%s: empty; no data\n", bits_path);
		exit(EXIT_FAILURE);
	}
	assert(fseek(f, 0, SEEK_SET) == 0);

	const long filesize_in_bits = 8*filesize;
	const long n_bits = head > 0 && head < filesize_in_bits ? head : filesize_in_bits;
	const long n_bytes = (n_bits+7)/8;
	uint8_t* data = malloc(n_bytes);
	assert(fread(data, n_bytes, 1, f) == 1);

	assert(fclose(f) == 0);

	const int width = stride;
	const int height = (n_bits + width - 1) / width;

	const int bpp = 3;
	uint8_t* bitmap = calloc(width*height, bpp);

	int bit_index = 0;
	uint8_t* wp = bitmap;
	for (int y = 0; y < height; y++) {
		const int is_ytick = (y%8) == 0;
		for (int x = 0; x < width; x++) {
			if (bit_index < n_bits) {
				const int is_set = data[bit_index >> 3] & (1 << (7-(bit_index&7)));
				if (is_set) {
					*(wp++) = 255;
					*(wp++) = 255;
					*(wp++) = is_ytick ? 200 : 255;
				} else {
					*(wp++) = is_ytick ? 150 : 0;
					*(wp++) = 0;
					*(wp++) = 128;
				}
				bit_index++;
			} else {
				*(wp++) = 255;
				*(wp++) = 0;
				*(wp++) = 255;
			}
		}
	}

	printf("writing %s.png...\n", png_path);
	stbi_write_png(png_path, width, height, bpp, bitmap, bpp*width);

	return EXIT_SUCCESS;
}
