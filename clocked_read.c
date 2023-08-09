#include "hardware/dma.h"

#include "clocked_read.h"
#include "clocked_read.pio.h"
#include "base.h"
#include "pin_config.h"

static uint8_t buffer[CLOCKED_READ_BUFFER_COUNT][MAX_DATA_BUFFER_SIZE];
static unsigned buffer_size[CLOCKED_READ_BUFFER_COUNT];
static enum buffer_status buffer_status[CLOCKED_READ_BUFFER_COUNT];
static char buffer_filename[CLOCKED_READ_BUFFER_COUNT][CLOCKED_READ_BUFFER_FILENAME_MAX_LENGTH];
static PIO pio;
static uint sm;
static uint dma_channel;

static inline void clocked_read_program_init(PIO pio, uint sm, uint offset, uint pin0_data)
{
	pio_sm_config cfg = clocked_read_program_get_default_config(offset);
	sm_config_set_in_pins(&cfg, pin0_data);
	pio_sm_set_consecutive_pindirs(pio, sm, pin0_data, /*pin_count=*/2, /*is_out=*/false);
	pio_gpio_init(pio, pin0_data);
	pio_gpio_init(pio, pin0_data + 1); // clk
	//
	sm_config_set_in_shift(&cfg, /*shift_right=*/true, /*autopush=*/true, /*push_threshold=*/32);
	sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_RX);
	//
	pio_sm_init(pio, sm, offset, &cfg);
}
//
static inline uint clocked_read_program_add_and_get_sm(PIO pio, uint pin0_data)
{
	const uint offset = pio_add_program(pio, &clocked_read_program);
	const uint sm = pio_claim_unused_sm(pio, true);
	clocked_read_program_init(pio, sm, offset, pin0_data);
	return sm;
}

void clocked_read_init(PIO _pio, uint _dma_channel)
{
	pio = _pio;
	dma_channel = _dma_channel;
	sm = clocked_read_program_add_and_get_sm(pio, GPIO_READ_DATA);
}

static void check_buffer_index(unsigned buffer_index)
{
	if (buffer_index >= CLOCKED_READ_BUFFER_COUNT) PANIC(PANIC_BOUNDS_CHECK_FAILED);
}

void clocked_read_into_buffer(unsigned buffer_index, unsigned word_32bit_count)
{
	check_buffer_index(buffer_index);
	if (buffer_status[buffer_index] != BUSY) PANIC(PANIC_UNEXPECTED_STATE);

	pio_sm_set_enabled(pio, sm, false);

	pio_sm_clear_fifos(pio, sm);
	pio_sm_restart(pio, sm);

	dma_channel_config dma_channel_cfg = dma_channel_get_default_config(dma_channel);
	channel_config_set_read_increment(&dma_channel_cfg,  false);
	channel_config_set_write_increment(&dma_channel_cfg, true);
	channel_config_set_dreq(&dma_channel_cfg, pio_get_dreq(pio, sm, false));

	const unsigned max_word_32bit_count =  MAX_DATA_BUFFER_SIZE >> 2;
	if (word_32bit_count > max_word_32bit_count) {
		word_32bit_count = max_word_32bit_count;
	}
	dma_channel_configure(
		dma_channel,
		&dma_channel_cfg,
		buffer[buffer_index], // write to buffer
		&pio->rxf[sm],             // read from PIO RX FIFO
		word_32bit_count,
		true // start now!
	);

	pio_sm_set_enabled(pio, sm, true);
}

int clocked_read_is_running(void)
{
	return dma_channel_is_busy(dma_channel);
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
