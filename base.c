#include <stdio.h>
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "controller_protocol.h"
#include "base.h"

void set_led(int p)
{
	gpio_put(LED_PIN, !!p);
}

void blink(int on_ms, int off_ms)
{
	set_led(1);
	if (on_ms > 0) sleep_ms(on_ms);
	set_led(0);
	if (off_ms > 0) sleep_ms(off_ms);
}

void assert_handler(const char* msg, const char* file, int line)
{
	multicore_reset_core1();
	while (1) {
		set_led(1);
		printf(CPPP_ERROR "ASSERTION FAILED: %s as %s:%d\n", msg, file, line);
		sleep_ms(1000);
		set_led(0);
		sleep_ms(1000);
	}
}
