// blink codes for PANIC()
#define PANIC_XXX                    (0xAA)
#define PANIC_ASSERTION_FAILED       (0xAF)
#define PANIC_STOP                   (0xA5)

// deps
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"

// local
#include "util.h"
#include "low_level_controller.pio.h"

const uint LED_PIN = PICO_DEFAULT_LED_PIN;
static void set_led(int x)
{
	gpio_put(LED_PIN, !!x);
}

static void blink(int on_ms, int off_ms)
{
	set_led(1);
	if (on_ms > 0) sleep_ms(on_ms);
	set_led(0);
	if (off_ms > 0) sleep_ms(off_ms);
}

// Panic and display error code using blink codes with the following repeating
// sequence:
//  - Fast strobe (signalling start of digit sequence)
//  - Pause
//  - For each non-zero hex digit, most significant first:
//     - Show hex digit as a series of blinks, one for 0x1, two for 0x2, etc
//     - Pause
// ( inspired by tinkering with an Audi 100 :-)
__attribute__ ((noreturn))
static void PANIC(uint32_t error)
{
	const int strobe_on_ms = 40;
	const int strobe_off_ms = 40;
	const int strobe_duration_ms = 500;
	const int digit_on_ms = 300;
	const int digit_off_ms = 200;
	const int pause_ms = 1500;
	const int n_hex_digits = sizeof(error)*2;
	while (1) {
		// strobe
		for (int t = 0; t <= strobe_duration_ms; t += (strobe_on_ms+strobe_off_ms)) {
			blink(strobe_on_ms, strobe_off_ms);
		}

		sleep_ms(pause_ms);

		// digits
		for (int i0 = 0; i0 < n_hex_digits; i0++) {
			int digit = (error >> (((n_hex_digits-1)-i0)<<2)) & 0xf;
			if (digit == 0) continue;
			for (int i1 = 0; i1 < digit; i1++) {
				blink(digit_on_ms, digit_off_ms);
			}
			sleep_ms(pause_ms);
		}
	}
}

int main()
{
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 0);

	stdio_init_all();

	blink(50, 0); // "Hi, we're up!"

	for (;;) {
		printf("hello world!\r\n");
		sleep_ms(500);
	}

	PANIC(PANIC_STOP);
}
