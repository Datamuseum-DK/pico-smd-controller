#ifndef CLOCKED_READ_H

#include <stdint.h>
#include "hardware/pio.h"

#include "drive.h"
#include "controller_protocol.h"

#define CLOCKED_READ_BUFFER_COUNT   3
#define CLOCKED_READ_BUFFER_FILENAME_MAX_LENGTH (128)

enum buffer_status {
	FREE,
	BUSY, // "allocated" or "writing"
	WRITTEN,
};

unsigned can_allocate_buffer(void);
unsigned allocate_buffer(unsigned size);
uint8_t* get_buffer_data(unsigned buffer_index);
char* get_buffer_filename(unsigned buffer_index);
enum buffer_status get_buffer_status(unsigned buffer_index);
void release_buffer(unsigned buffer_index);
void wrote_buffer(unsigned buffer_index);
int get_written_buffer_index(void);
unsigned get_buffer_size(unsigned buffer_index);
void reset_buffers(void);

#define CLOCKED_READ_H
#endif
