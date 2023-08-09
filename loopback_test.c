#include <stdio.h>
#include "hardware/dma.h"

#include "loopback_test.h"
#include "loopback_test.pio.h"
#include "controller_protocol.h"
#include "clocked_read.h"
#include "base.h"

static PIO pio;
static uint sm;
static uint dma_channel;
static int fired;
static absolute_time_t t0;

static void clear_gpio(void)
{
	gpio_put(GPIO_LOOPBACK_TEST_DATA,  0);
	gpio_put(GPIO_LOOPBACK_TEST_CLOCK, 0);
}

void loopback_test_prep(PIO _pio, uint _dma_channel)
{
	pio = _pio;
	dma_channel = _dma_channel;
	const uint offset = pio_add_program(pio, &loopback_test_program);
	sm = pio_claim_unused_sm(pio, true);

	pio_sm_set_consecutive_pindirs(pio, sm, GPIO_LOOPBACK_TEST_DATA,  /*pin_count=*/1, /*is_out=*/true);
	pio_sm_set_consecutive_pindirs(pio, sm, GPIO_LOOPBACK_TEST_CLOCK, /*pin_count=*/1, /*is_out=*/true);

	pio_sm_config cfg = loopback_test_program_get_default_config(0);
	sm_config_set_clkdiv_int_frac(&cfg, 6, 0); // 125MHz/6=20.83MHz; two cycles per loop
	sm_config_set_out_shift(&cfg, /*shift_right=*/true, /*autopull*/true, /*pull_threshold=*/32);
	sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);
	sm_config_set_out_pins(&cfg, GPIO_LOOPBACK_TEST_DATA, 1);
	sm_config_set_sideset_pins(&cfg, GPIO_LOOPBACK_TEST_CLOCK);

	pio_sm_init(pio, sm, offset, &cfg);
}

uint8_t loopback_buffer[10000];
void loopback_test_fire(uint n_bytes)
{
	if (n_bytes > ARRAY_LENGTH(loopback_buffer)) n_bytes = ARRAY_LENGTH(loopback_buffer);

	clear_gpio();

	loopback_test_generate_data(loopback_buffer, n_bytes);

	pio_gpio_init(pio, GPIO_LOOPBACK_TEST_DATA); // translates to gpio_set_function(GPIO_LOOPBACK_TEST_DATA, PIO0/1)
	pio_gpio_init(pio, GPIO_LOOPBACK_TEST_CLOCK);

	pio_sm_set_enabled(pio, sm, true);

	dma_channel_config dma_channel_cfg = dma_channel_get_default_config(dma_channel);
	channel_config_set_read_increment(&dma_channel_cfg,  true);
	channel_config_set_write_increment(&dma_channel_cfg, false);
	channel_config_set_dreq(&dma_channel_cfg, pio_get_dreq(pio, sm, true));
	dma_channel_configure(
		dma_channel,
		&dma_channel_cfg,
		/*write_addr=*/&pio->txf[sm],
		/*read_addr=*/loopback_buffer,
		/*transfer_count*/n_bytes>>2,
		true // start now!
	);

	t0 = get_absolute_time();
	fired = 1;
}

void loopback_test_tick(void)
{
	if (!fired || dma_channel_is_busy(dma_channel)) return;
	const absolute_time_t dt = get_absolute_time() - t0;
	fired = 0;

	gpio_set_function(GPIO_LOOPBACK_TEST_DATA,  GPIO_FUNC_SIO);
	gpio_set_function(GPIO_LOOPBACK_TEST_CLOCK, GPIO_FUNC_SIO);
	clear_gpio();

	pio_sm_set_enabled(pio, sm, false);

	printf(CPPP_INFO "loopback test done in %llu microseconds\n", dt);
}
