#include "hardware/dma.h"

#include "clocked_read.h"
#include "clocked_read.pio.h"
#include "base.h"
#include "config.h"

#define CLOCKED_READ_PIO         pio0
#define CLOCKED_READ_DMA_CHANNEL (0)
uint8_t clocked_read_buffer[CLOCKED_READ_BUFFER_COUNT][CLOCKED_READ_BUFFER_SIZE];
uint clocked_read_sm;

void clocked_read_init(void)
{
	clocked_read_sm = clocked_read_program_add_and_get_sm(CLOCKED_READ_PIO, GPIO_READ_DATA);
}

void clocked_read_into_buffer(unsigned buffer_index)
{
	if (!(0 <= buffer_index && buffer_index < CLOCKED_READ_BUFFER_COUNT)) PANIC(PANIC_BOUNDS_CHECK_FAILED);

	const PIO pio = CLOCKED_READ_PIO;
	const uint sm = clocked_read_sm;
	const uint dma_channel = CLOCKED_READ_DMA_CHANNEL;

	pio_sm_set_enabled(pio, sm, false);
	pio_sm_clear_fifos(pio, sm);
	pio_sm_restart(pio, sm);

	dma_channel_config dma_channel_cfg = dma_channel_get_default_config(dma_channel);
	channel_config_set_read_increment(&dma_channel_cfg,  false);
	channel_config_set_write_increment(&dma_channel_cfg, true);
	channel_config_set_dreq(&dma_channel_cfg, pio_get_dreq(pio, sm, false));

	_Static_assert((CLOCKED_READ_WORD_SIZE_IN_BITS_LOG2-3) >= 0, "sub byte word size?!");
	const unsigned word_count =  CLOCKED_READ_BUFFER_SIZE >> (CLOCKED_READ_WORD_SIZE_IN_BITS_LOG2-3);
	dma_channel_configure(
		dma_channel,
		&dma_channel_cfg,
		clocked_read_buffer[buffer_index], // write to buffer
		&pio->rxf[sm],                     // read from PIO RX FIFO
		word_count,
		true // start now!
	);

	pio_sm_set_enabled(pio, sm, true);
}

uint8_t* clocked_read_get_buffer(unsigned buffer_index)
{
	return clocked_read_buffer[buffer_index % CLOCKED_READ_BUFFER_COUNT];
}
