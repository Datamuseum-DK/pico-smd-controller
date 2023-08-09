#ifndef LOOPBACK_TEST_H

#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "pin_config.h"

#define GPIO_LOOPBACK_TEST_DATA  (GPIO_BIT0)
#define GPIO_LOOPBACK_TEST_CLOCK (GPIO_BIT1)

void loopback_test_prep(PIO pio, uint dma_channel);
void loopback_test_fire(uint n_bytes);
void loopback_test_tick(void);

#include "loopback_test_generate_data.h"

#define LOOPBACK_TEST_H
#endif
