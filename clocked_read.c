#include "hardware/dma.h"

#include "clocked_read.h"
#include "clocked_read.pio.h"
#include "base.h"
#include "pin_config.h"

#define READ_PIO         pio0
#define READ_DMA_CHANNEL (0)
uint8_t read_buffer[CLOCKED_READ_BUFFER_COUNT][CLOCKED_READ_BUFFER_SIZE];
unsigned read_buffer_size[CLOCKED_READ_BUFFER_COUNT];
enum buffer_status read_buffer_status[CLOCKED_READ_BUFFER_COUNT];
char read_buffer_filename[CLOCKED_READ_BUFFER_COUNT][CLOCKED_READ_BUFFER_FILENAME_MAX_LENGTH];
uint read_sm;

void clocked_read_init(void)
{
	read_sm = clocked_read_program_add_and_get_sm(READ_PIO, GPIO_READ_DATA);
}

static void check_buffer_index(unsigned buffer_index)
{
	if (buffer_index >= CLOCKED_READ_BUFFER_COUNT) PANIC(PANIC_BOUNDS_CHECK_FAILED);
}

void clocked_read_into_buffer(unsigned buffer_index, unsigned word_32bit_count)
{
	check_buffer_index(buffer_index);
	if (read_buffer_status[buffer_index] != BUSY) PANIC(PANIC_UNEXPECTED_STATE);

	const PIO pio = READ_PIO;
	const uint sm = read_sm;

	pio_sm_set_enabled(pio, sm, false);
	pio_sm_clear_fifos(pio, sm);
	pio_sm_restart(pio, sm);

	dma_channel_config dma_channel_cfg = dma_channel_get_default_config(READ_DMA_CHANNEL);
	channel_config_set_read_increment(&dma_channel_cfg,  false);
	channel_config_set_write_increment(&dma_channel_cfg, true);
	channel_config_set_dreq(&dma_channel_cfg, pio_get_dreq(pio, sm, false));

	_Static_assert((CLOCKED_READ_WORD_SIZE_IN_BITS_LOG2-3) >= 0, "sub byte word size?!");
	const unsigned max_word_32bit_count =  CLOCKED_READ_BUFFER_SIZE >> (CLOCKED_READ_WORD_SIZE_IN_BITS_LOG2-3);
	if (word_32bit_count > max_word_32bit_count) {
		word_32bit_count = max_word_32bit_count;
	}
	dma_channel_configure(
		READ_DMA_CHANNEL,
		&dma_channel_cfg,
		read_buffer[buffer_index], // write to buffer
		&pio->rxf[sm],             // read from PIO RX FIFO
		word_32bit_count,
		true // start now!
	);

	pio_sm_set_enabled(pio, sm, true);
}

int clocked_read_is_running(void)
{
	return dma_channel_is_busy(READ_DMA_CHANNEL);
}

static int find_buffer_index(enum buffer_status with_buffer_status)
{
	for (unsigned i = 0; i < CLOCKED_READ_BUFFER_COUNT; i++) {
		if (read_buffer_status[i] == with_buffer_status) {
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
	int i = get_next_free_buffer_index();
	if (i < 0) PANIC(PANIC_ALLOCATION_ERROR);
	check_buffer_index(i);
	read_buffer_status[i] = BUSY;
	if (size > CLOCKED_READ_BUFFER_SIZE) size = CLOCKED_READ_BUFFER_SIZE;
	read_buffer_size[i] = size;
	return i;
}

uint8_t* get_buffer_data(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	return read_buffer[buffer_index];
}

char* get_buffer_filename(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	return read_buffer_filename[buffer_index];
}

enum buffer_status get_buffer_status(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	return read_buffer_status[buffer_index];
}

void release_buffer(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	if (read_buffer_status[buffer_index] != WRITTEN) PANIC(PANIC_UNEXPECTED_STATE);
	read_buffer_status[buffer_index] = FREE;
}

void wrote_buffer(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	if (read_buffer_status[buffer_index] != BUSY) PANIC(PANIC_UNEXPECTED_STATE);
	read_buffer_status[buffer_index] = WRITTEN;
}

unsigned get_buffer_size(unsigned buffer_index)
{
	check_buffer_index(buffer_index);
	return read_buffer_size[buffer_index];
}
