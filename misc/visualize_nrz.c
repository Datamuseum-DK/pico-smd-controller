// cc -I.. visualize_nrz.c -o visualize_nrz
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "drive.h"

int main(int argc, char** argv)
{
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <path/to/file.nrz> <path/to/out.png>\n", argv[0]);
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

	uint8_t* data = malloc(sz);
	assert(fread(data, sz, 1, f) == 1);

	assert(fclose(f) == 0);

	const int width = 1536;
	const int n_compar = (sz+DRIVE_BYTES_PER_TRACK-1)/DRIVE_BYTES_PER_TRACK;
	const int n_bits_per_track = 8*DRIVE_BYTES_PER_TRACK;
	const int n_rows = (n_bits_per_track + width - 1) / width;
	const int padding = 2;
	const int row_height = (n_compar+padding);
	const int height = row_height * n_rows;
	const int bpp = 3;

	uint8_t* bitmap = calloc(width*height, bpp);

	for (int row = 0; row < n_rows; row++) {
		for (int x = 0; x < width; x++) {
			for (int i = 0; i < n_compar; i++) {
				const int bit_index = (row*width+x) + i*n_bits_per_track;
				const int is_set = data[bit_index >> 3] & (1 << (7-(bit_index&7)));
				char* p = &bitmap[((row*row_height+i)*width + x) * bpp];
				if (is_set) {
					p[0] = 255;
					p[1] = 255;
					p[2] = 255;
				} else {
					p[0] = 0;
					p[1] = 0;
					p[2] = 200;
				}
			}
			for (int i = 0; i < padding; i++) {
				char* p = &bitmap[((row*row_height+n_compar+i)*width + x) * bpp];
				p[0] = 100;
				p[1] = 0;
				p[2] = 0;
			}
		}
	}

	printf("writing %s.png...\n", argv[2]);
	stbi_write_png(argv[2], width, height, bpp, bitmap, bpp*width);

	return EXIT_SUCCESS;
}
