#include "hardware/dma.h"

#include "clocked_read.h"
#include "clocked_read.pio.h"
#include "base.h"
#include "pin_config.h"

static uint8_t buffer[CLOCKED_READ_BUFFER_COUNT][MAX_DATA_BUFFER_SIZE];
static unsigned buffer_size[CLOCKED_READ_BUFFER_COUNT];
static enum buffer_status buffer_status[CLOCKED_READ_BUFFER_COUNT];
static char buffer_filename[CLOCKED_READ_BUFFER_COUNT][CLOCKED_READ_BUFFER_FILENAME_MAX_LENGTH];

static void check_buffer_index(unsigned buffer_index)
{
	if (buffer_index >= CLOCKED_READ_BUFFER_COUNT) PANIC(PANIC_BOUNDS_CHECK_FAILED);
}

static int find_buffer_index(enum buffer_status with_buffer_status)
{
	for (unsigned i = 0; i < CLOCKED_READ_BUFFER_COUNT; i++) {
		if (buffer_status[i] == with_buffer_status) {
			return i;
		}
	}
	return -1;
}

static int get_next_free_buffer_index(void)
{
	return find_buffer_index(FREE);
}

int get_written_buffer_index(void)
{
	return find_buffer_index(WRITTEN);
}

unsigned can_allocate_buffer(void)
{
	return get_next_free_buffer_index() >= 0;
}

unsigned allocate_buffer(unsigned size)
{
	const int i = get_next_free_buffer_index();
	if (i < 0) PANIC(PANIC_ALLOCATION_ERROR);
	check_buffer_index(i);
	buffer_status[i] = BUSY;
	if (size > MAX_DATA_BUFFER_SIZE) size = MAX_DATA_BUFFER_SIZE;
	buffer_size[i] = size;
	return i;
}

uint8_t* get_buffer_data(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	return buffer[buffer_index];
}

char* get_buffer_filename(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	return buffer_filename[buffer_index];
}

enum buffer_status get_buffer_status(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	return buffer_status[buffer_index];
}

void release_buffer(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	if (buffer_status[buffer_index] != WRITTEN) PANIC(PANIC_UNEXPECTED_STATE);
	buffer_status[buffer_index] = FREE;
}

void wrote_buffer(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	if (buffer_status[buffer_index] != BUSY) PANIC(PANIC_UNEXPECTED_STATE);
	buffer_status[buffer_index] = WRITTEN;
}

unsigned get_buffer_size(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	return buffer_size[buffer_index];
}

void reset_buffers(void)
{
	for (int i = 0; i < CLOCKED_READ_BUFFER_COUNT; i++) {
		buffer_status[i] = FREE;
	}
}
