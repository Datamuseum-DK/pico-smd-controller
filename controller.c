#include "low_level_controller.pio.h"
#include "pico/stdlib.h"

static void blink_test(void)
{
	const uint LED_PIN = PICO_DEFAULT_LED_PIN;
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	while (true) {
		gpio_put(LED_PIN, 1);
		sleep_ms(250);
		gpio_put(LED_PIN, 0);
		sleep_ms(250);
	}
}

int main()
{
	// TODO
	blink_test();
	return 0;
}
