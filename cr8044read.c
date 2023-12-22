#include "hardware/dma.h"

#include "cr8044read.h"
#include "cr8044read.pio.h"
#include "pin_config.h"
#include "base.h"
#include "controller_protocol.h"

_Static_assert(cr8044read_READ_DATA == GPIO_READ_DATA);
_Static_assert(cr8044read_READ_CLOCK == GPIO_READ_CLOCK);
_Static_assert(cr8044read_SECTOR == GPIO_SECTOR);
_Static_assert(cr8044read_SERVO_CLOCK == GPIO_SERVO_CLOCK);

static PIO pio;
static uint sm;
static uint dma_channel;
static uint dma_channel2;

#define N_PULL_WORDS_PER_SECTOR (5)
#define N_PULL_WORDS (N_PULL_WORDS_PER_SECTOR * CR8044READ_N_SECTORS)

static unsigned pull_words[N_PULL_WORDS];

static inline void cr8044read_program_init(PIO pio, uint sm, uint offset)
{
	pio_sm_config cfg = cr8044read_program_get_default_config(offset);
	sm_config_set_in_pins(&cfg, GPIO_READ_DATA);
	sm_config_set_out_pins(&cfg, 0, 32);

	pio_sm_set_consecutive_pindirs(pio, sm, GPIO_BIT1, /*pin_count=*/1, /*is_out=*/true);
	pio_sm_set_consecutive_pindirs(pio, sm, GPIO_BIT2, /*pin_count=*/1, /*is_out=*/true);
	pio_sm_set_consecutive_pindirs(pio, sm, GPIO_BIT3, /*pin_count=*/1, /*is_out=*/true);
	pio_sm_set_consecutive_pindirs(pio, sm, GPIO_BIT7, /*pin_count=*/1, /*is_out=*/true);
	pio_sm_set_consecutive_pindirs(pio, sm, GPIO_BIT8, /*pin_count=*/1, /*is_out=*/true);

	sm_config_set_in_shift(&cfg, /*shift_right=*/true, /*autopush=*/true, /*push_threshold=*/32);
	sm_config_set_out_shift(&cfg, /*shift_right=*/false, /*autopull=*/false, /*pull_threshold=*/32);
	sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_NONE);

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

void cr8044read_init(PIO _pio, uint _dma_channel, uint _dma_channel2)
{
	pio = _pio;
	dma_channel = _dma_channel;
	dma_channel2 = _dma_channel2;
	sm = cr8044read_program_add_and_get_sm(pio);
}

void cr8044read_prep(unsigned extra_read_enable_bits)
{
	const unsigned read_enable_gpio = GPIO_BIT1 | extra_read_enable_bits;
	const unsigned read_disable_gpio = 0;
	unsigned* wp = pull_words;
	for (int i0 = 0; i0 < CR8044READ_N_SECTORS; i0++) {
		*(wp++) = read_enable_gpio;
		*(wp++) = read_disable_gpio;
		*(wp++) = read_enable_gpio;
		*(wp++) = (((CR8044READ_DATA_SIZE+3)>>2) << 5) - 1;
		*(wp++) = read_disable_gpio;
	}
	if ((wp-pull_words) != N_PULL_WORDS) PANIC(PANIC_UNEXPECTED_STATE);
}

void cr8044read_execute(uint8_t* dst)
{
	pio_sm_set_enabled(pio, sm, false);

	// PIO must be able to drive these pins. these calls are like calling
	// gpio_set_function() with the corresponding pio (PIO0/1)
	pio_gpio_init(pio, GPIO_BIT1); // read gate
	pio_gpio_init(pio, GPIO_BIT2); // servo offset positive
	pio_gpio_init(pio, GPIO_BIT3); // servo offset negative
	pio_gpio_init(pio, GPIO_BIT7); // data strobe early
	pio_gpio_init(pio, GPIO_BIT8); // data strobe late

	pio_sm_clear_fifos(pio, sm);
	pio_sm_restart(pio, sm);

	const int word_32bit_count = (CR8044READ_BYTES_TOTAL + 3) >> 2;

	{
		dma_channel_config dma_channel_cfg = dma_channel_get_default_config(dma_channel);
		channel_config_set_read_increment(&dma_channel_cfg,  false);
		channel_config_set_write_increment(&dma_channel_cfg, true);
		channel_config_set_dreq(&dma_channel_cfg, pio_get_dreq(pio, sm, false));
		dma_channel_configure(
			dma_channel,
			&dma_channel_cfg,
			dst,
			&pio->rxf[sm],             // read from PIO RX FIFO
			word_32bit_count,
			true // start now!
		);
	}

	{
		dma_channel_config dma_channel2_cfg = dma_channel_get_default_config(dma_channel2);
		channel_config_set_read_increment(&dma_channel2_cfg,  true);
		channel_config_set_write_increment(&dma_channel2_cfg, false);
		channel_config_set_dreq(&dma_channel2_cfg, pio_get_dreq(pio, sm, true));
		dma_channel_configure(
			dma_channel2,
			&dma_channel2_cfg,
			&pio->txf[sm],             // write to PIO TX FIFO
			pull_words,
			N_PULL_WORDS,
			true // start now!
		);
	}

	pio_sm_set_enabled(pio, sm, true);
	while (dma_channel_is_busy(dma_channel)) {};
	pio_sm_set_enabled(pio, sm, false);

	// reset the effect of calling pio_gpio_init() above so that software
	// can drive these pins again
	gpio_set_function(GPIO_BIT1, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_BIT2, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_BIT3, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_BIT7, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_BIT8, GPIO_FUNC_SIO);
}
