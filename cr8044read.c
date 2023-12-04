#include "hardware/dma.h"

#include "cr8044read.h"
#include "cr8044read.pio.h"
#include "pin_config.h"

_Static_assert(cr8044read_READ_DATA == GPIO_READ_DATA);
_Static_assert(cr8044read_READ_CLOCK == GPIO_READ_CLOCK);
_Static_assert(cr8044read_SECTOR == GPIO_SECTOR);
_Static_assert(cr8044read_SERVO_CLOCK == GPIO_SERVO_CLOCK);

static PIO pio;
static uint sm;
static uint dma_channel;

static inline void cr8044read_program_init(PIO pio, uint sm, uint offset)
{
	pio_sm_config cfg = cr8044read_program_get_default_config(offset);
	sm_config_set_in_pins(&cfg, GPIO_READ_DATA);
	pio_gpio_init(pio, 0);
	//
	sm_config_set_in_shift(&cfg, /*shift_right=*/true, /*autopush=*/true, /*push_threshold=*/32);
	sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_NONE);
	//
	pio_sm_init(pio, sm, offset, &cfg);
}
//
static inline uint cr8044read_program_add_and_get_sm(PIO pio)
{
	const uint offset = pio_add_program(pio, &cr8044read_program);
	const uint sm = pio_claim_unused_sm(pio, true);
	cr8044read_program_init(pio, sm, offset);
	return sm;
}

void cr8044read_init(PIO _pio, uint _dma_channel)
{
	pio = _pio;
	dma_channel = _dma_channel;
	sm = cr8044read_program_add_and_get_sm(pio);
}

static void feed_sm_pull(int index, unsigned ex)
{
	const unsigned all_gpio = GPIO_UNIT_SELECT_TAG; // must be high even during read disable
	const unsigned enable_gpio = GPIO_TAG3 | GPIO_BIT1; // read gate
	const unsigned read_enable_gpio  = all_gpio | enable_gpio | ex;
	const unsigned read_disable_gpio = all_gpio;
	switch (index%5) {
	case 0: pio_sm_put_blocking(pio, sm, read_enable_gpio); break;
	case 1: pio_sm_put_blocking(pio, sm, read_disable_gpio); break;
	case 2: pio_sm_put_blocking(pio, sm, read_enable_gpio); break;
	case 3: pio_sm_put_blocking(pio, sm, ((CR8044READ_DATA_SIZE+3)>>2) << 5); break;
	case 4: pio_sm_put_blocking(pio, sm, read_disable_gpio); break;
	}
}

void cr8044read_execute(uint8_t* dst, unsigned extra_read_enable_bits)
{
	pio_sm_set_enabled(pio, sm, false);

	pio_sm_clear_fifos(pio, sm);
	pio_sm_restart(pio, sm);

	dma_channel_config dma_channel_cfg = dma_channel_get_default_config(dma_channel);
	channel_config_set_read_increment(&dma_channel_cfg,  false);
	channel_config_set_write_increment(&dma_channel_cfg, true);
	channel_config_set_dreq(&dma_channel_cfg, pio_get_dreq(pio, sm, false));

	for (int i = 0; i < 4; i++) feed_sm_pull(i, extra_read_enable_bits);

	const int word_32bit_count = (CR8044READ_BYTES_TOTAL + 3) >> 2;
	dma_channel_configure(
		dma_channel,
		&dma_channel_cfg,
		dst,
		&pio->rxf[sm],             // read from PIO RX FIFO
		word_32bit_count,
		true // start now!
	);

	pio_sm_set_enabled(pio, sm, true);

	for (int i = 4; i < (CR8044READ_N_SECTORS-1)*5; i++) feed_sm_pull(i, extra_read_enable_bits);

	while (dma_channel_is_busy(dma_channel)) {};
}
