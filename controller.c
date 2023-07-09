// deps
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/unique_id.h"

// local
#include "config.h"
#include "base.h"
#include "command_parser.h"
#include "low_level_controller.pio.h"

unsigned core1_counter = 0;
void core1_entry() {
	for (;;) {
		core1_counter++;
		sleep_ms(1);
		tight_loop_contents();
	}
}

static inline int gpio_type_to_dir(enum gpio_type t)
{
	switch (t) {
	case DATA:     return GPIO_IN;
	case STATUS:   return GPIO_IN;
	case CTRL:     return GPIO_OUT;
	default: PANIC(PANIC_XXX);
	}
}

unsigned stdin_received_bytes = 0;
unsigned subscription_mask = 0;

struct command_parser command_parser;

int main()
{
	// I/O pin config
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 0);
	#define PIN(TYPE, NAME, GPN) \
		gpio_init(GPN); \
		gpio_set_dir(GPN, gpio_type_to_dir(TYPE)); \
		if (gpio_type_to_dir(TYPE) == GPIO_OUT) gpio_put(GPN, 0);
	EMIT_PIN_CONFIG
	#undef PIN

	stdio_init_all();
	multicore_launch_core1(core1_entry);

	blink(50, 0); // "Hi, we're up!"

	for (;;) {
		int got_char = getchar_timeout_us(0);
		if (got_char == PICO_ERROR_TIMEOUT || got_char == 0 || got_char >= 256) {
			tight_loop_contents();
			continue;
		}
		stdin_received_bytes++;
		if (command_parser_put_char(&command_parser, got_char)) {
			switch (command_parser.command) {
			case COMMAND_led: {
				set_led(command_parser.arguments[0].u);
			} break;
			case COMMAND_get_status_descriptors: {
				printf(CTPP_STATUS_DESCRIPTORS);
				#define PIN(TYPE, NAME, GPN) if (TYPE == STATUS) printf(":%s", #NAME);
				EMIT_PIN_CONFIG
				#undef PIN
				printf("\n");
			} break;
			case COMMAND_subscribe: {
				subscription_mask = command_parser.arguments[0].u;
			} break;
			default: {
				printf(CTPP_ERROR "unhandled command %s/%d\n",
					command_to_string(command_parser.command),
					command_parser.command);
			} break;
			}
		}
	}

	PANIC(PANIC_STOP);
}
