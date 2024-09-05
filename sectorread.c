#include <stdio.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "pico/time.h"

#include "base.h"
#include "sectorread.h"
#include "sectorread.pio.h"
#include "pin_config.h"
#include "controller_protocol.h"

#define MAX_PULL_WORDS (256)
static unsigned pull_words[MAX_PULL_WORDS];
static int n_pull_words;
static int n_total_bits;

static PIO pio;
static uint sm;
static uint dma_channel;
static uint dma_channel2;
static uint pc_offset;

static inline void sectorread_program_init(PIO pio, uint sm, uint pc_offset)
{
	pio_sm_config cfg = sectorread_program_get_default_config(pc_offset);
	sm_config_set_in_pins(&cfg, GPIO_READ_DATA);
	sm_config_set_set_pins(&cfg, GPIO_BIT1, 1);

	pio_sm_set_consecutive_pindirs(pio, sm, GPIO_BIT1,   /*pin_count=*/1, /*is_out=*/true);

	sm_config_set_in_shift(&cfg, /*shift_right=*/true, /*autopush=*/true, /*push_threshold=*/32);
	sm_config_set_out_shift(&cfg, /*shift_right=*/false, /*autopull=*/false, /*pull_threshold=*/32);
	sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_NONE);

	pio_sm_init(pio, sm, pc_offset, &cfg);
}
//
static inline uint sectorread_program_add_and_get_sm(PIO pio)
{
	pc_offset = pio_add_program(pio, &sectorread_program);
	const uint sm = pio_claim_unused_sm(pio, true);
	sectorread_program_init(pio, sm, pc_offset);
	return sm;
}

void sectorread_init(PIO _pio, uint _dma_channel, uint _dma_channel2, int n_segments, struct segment* segments)
{
	n_pull_words = n_segments*2;
	ASSERT(n_pull_words <= MAX_PULL_WORDS);
	unsigned* wp = pull_words;
	n_total_bits = 0;
	for (int i0 = 0; i0 < n_segments; i0++) {
		struct segment* segment = &segments[i0];
		ASSERT((segment->wait_bits > 0) && "cannot have wait_bits==0");
		ASSERT((segment->data_bits > 0) && "cannot have data_bits==0");
		*(wp++) = segment->wait_bits - 1;
		*(wp++) = segment->data_bits - 1;
		n_total_bits += segment->data_bits;
	}
	pio = _pio;
	dma_channel = _dma_channel;
	dma_channel2 = _dma_channel2;
	sm = sectorread_program_add_and_get_sm(pio);
}

void sectorread_execute(uint8_t* dst)
{
	pio_sm_set_enabled(pio, sm, false);

	pio_gpio_init(pio, GPIO_BIT1);

	pio_sm_clear_fifos(pio, sm);
	pio_sm_restart(pio, sm);
	pio_sm_exec(pio, sm, pio_encode_jmp(pc_offset));

	const int word_32bit_count = n_total_bits >> 5;

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
			n_pull_words,
			true // start now!
		);
	}

	pio_sm_set_enabled(pio, sm, true);

	absolute_time_t t0 = get_absolute_time();
	while (dma_channel_is_busy(dma_channel)) {
		absolute_time_t dt = get_absolute_time() - t0;
		// NOTE: job should take at most 1/60 seconds
		if (dt > 500000LL) {
			printf(CPPP_INFO "ERROR: sectorread_execute() stalled // FDEBUG=%lu FSTAT=%lu ADDR=%lu\n",
				pio->fdebug,
				pio->fstat,
				pio->sm[sm].addr
				);
			break;
		}
	}
	pio_sm_set_enabled(pio, sm, false);

	// reset the effect of calling pio_gpio_init() above so that software
	// can drive these pins again
	gpio_set_function(GPIO_BIT1, GPIO_FUNC_SIO);
}
