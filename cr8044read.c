/*

This PIO/DMA code is specifically designed to read data written by a CR8044
disk controller.

Initially we attempted a naive/raw approach that simply read 5+ revolutions
after INDEX. This approach is still useful for analyzing data with an unknown
sector structure ("raw reads"), but in practice it can't make high quality data
extractions. The problem is that the data stream gets inverted at certain
points, and it seems probable that this point is where "historic" disk
controllers would begin writing sector data.

The CR8044 sector structure is:
   Gap-A  (31 bytes of zeroes)
   Address Field (cylinder, head, sector, checksum, bad sector flags, etc...)
   Gap-B  (19 bytes of zeroes)
   Data Field
   Gap-C  (17.5 bytes of zeroes)
Analysis of raw reads show that many inversions occur ~30 bits into Gap-B. It's
a good guess that the old CR8044 disk began writing sector data at this point.

The SMD manual states that the drive must read 7.75µs data zeroes when you
enable read gate in order for the drive to sync correctly. This corresponds to
~75 zero-bits (at 9.67MHz).

However, it seems this synchronization isn't stable enough to survive the
"write point" in Gap-B. So the solution is to turn read gate on/off at precise
points.

*/

/*
FIXME:
 - Program halts when CR8044READ_N_SECTORS > 32 - why?
 - Wait for INDEX doesn't really seem to work; reads are started at "random"
   sectors... yet the point is not completely random, because the first sector
   isn't a "garbage read".
*/

#include <stdio.h>

#include "hardware/dma.h"
#include "pico/time.h"

#include "cr8044read.h"
#include "cr8044read.pio.h"
#include "pin_config.h"
#include "base.h"
#include "controller_protocol.h"

_Static_assert(cr8044read_READ_DATA == GPIO_READ_DATA);
_Static_assert(cr8044read_READ_CLOCK == GPIO_READ_CLOCK);
_Static_assert(cr8044read_INDEX == GPIO_INDEX);
_Static_assert(cr8044read_SECTOR == GPIO_SECTOR);
_Static_assert(cr8044read_SERVO_CLOCK == GPIO_SERVO_CLOCK);

static PIO pio;
static uint sm;
static uint dma_channel;
static uint dma_channel2;

#define N_PULL_WORDS_PER_SECTOR (3)
#define N_PULL_WORDS (N_PULL_WORDS_PER_SECTOR * CR8044READ_N_SECTORS)

static unsigned pull_words[N_PULL_WORDS];

static inline void cr8044read_program_init(PIO pio, uint sm, uint offset)
{
	pio_sm_config cfg = cr8044read_program_get_default_config(offset);
	sm_config_set_in_pins(&cfg, GPIO_READ_DATA);
	sm_config_set_set_pins(&cfg, GPIO_BIT1, 1);

	pio_sm_set_consecutive_pindirs(pio, sm, GPIO_BIT1,   /*pin_count=*/1, /*is_out=*/true);

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
	unsigned* wp = pull_words;
	const unsigned n_address_bits = 8*9;
	const unsigned gap_b_wait = 32;
	const unsigned n_data_bits = (((CR8044READ_DATA_SIZE+3)>>2) << 5);
	for (int i0 = 0; i0 < CR8044READ_N_SECTORS; i0++) {
		// these are loaded into the PIO X-register and used for loop
		// counting. since loops are "repeat and decrement if
		// non-zero", the value must be one smaller than the intended
		// iteration count.
		*(wp++) = n_address_bits - 1;
		*(wp++) = gap_b_wait - 1;
		*(wp++) = n_data_bits - 1;
	}
	if ((wp-pull_words) != N_PULL_WORDS) PANIC(PANIC_UNEXPECTED_STATE);
	pio = _pio;
	dma_channel = _dma_channel;
	dma_channel2 = _dma_channel2;
	sm = cr8044read_program_add_and_get_sm(pio);
}

void cr8044read_execute(uint8_t* dst)
{
	pio_sm_set_enabled(pio, sm, false);

	pio_gpio_init(pio, GPIO_BIT1);

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

	absolute_time_t t0 = get_absolute_time();
	while (dma_channel_is_busy(dma_channel)) {
		absolute_time_t dt = get_absolute_time() - t0;
		// NOTE: job should take at most 1/60 seconds
		if (dt > 500000LL) {
			printf(CPPP_INFO "ERROR: cr8044read_execute() stalled\n");
			break;
		}
	}
	pio_sm_set_enabled(pio, sm, false);

	// reset the effect of calling pio_gpio_init() above so that software
	// can drive these pins again
	gpio_set_function(GPIO_BIT1, GPIO_FUNC_SIO);
}
