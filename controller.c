// deps
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/unique_id.h"
#include "tusb.h"

// local
#include "config.h"
#include "base.h"
#include "command_parser.h"
#include "low_level_controller.pio.h"

unsigned stdin_received_bytes = 0;
unsigned is_subscribing_to_status = 0;
struct command_parser command_parser;

#if 0
void core1_entry()
{
	for (;;) {
		tight_loop_contents();
	}
}
#endif

static inline int gpio_type_to_dir(enum gpio_type t)
{
	switch (t) {
	case DATA:     return GPIO_IN;
	case STATUS:   return GPIO_IN;
	case CTRL:     return GPIO_OUT;
	default: PANIC(PANIC_XXX);
	}
}

unsigned prev_status = 0;
absolute_time_t last_status_timestamp = 0;

static void status_housekeeping(void)
{
	const absolute_time_t t = get_absolute_time();

	{ // poll status pins
		unsigned status = 0;
		unsigned mask = 1;
		#define PIN(TYPE,NAME,GPN) \
			if (TYPE==STATUS) { \
				if (gpio_get(GPN)) status |= mask; \
				mask <<= 1; \
			}
		EMIT_PIN_CONFIG
		#undef PIN
		if (status != prev_status) {
			if (is_subscribing_to_status) {
				printf("%s %llu %d\n", CPPP_STATUS, t, status);
				last_status_timestamp = t;
			}
			prev_status = status;
		}
	}

	if ((t - last_status_timestamp) > (1000000/60)) {
		if (is_subscribing_to_status) {
			printf("%s %llu\n", CPPP_STATUS_TIME, t);
		}
		last_status_timestamp = t;
	}
}

int main()
{
	// I/O pin config
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 0);
	#define PIN(TYPE, NAME, GPN) \
		gpio_init(GPN); \
		gpio_set_dir(GPN, gpio_type_to_dir(TYPE)); \
		if (gpio_type_to_dir(TYPE) == GPIO_OUT) { \
			gpio_put(GPN, 0); \
		} else { \
			gpio_pull_down(GPN); \
		}
	EMIT_PIN_CONFIG
	#undef PIN

	stdio_init_all();
	#if 0
	multicore_launch_core1(core1_entry);
	#endif

	blink(50, 0); // "Hi, we're up!"

	for (;;) {
		tud_task();
		status_housekeeping();

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
				printf(CPPP_STATUS_DESCRIPTORS);
				#define PIN(TYPE, NAME, GPN) \
					if (TYPE == STATUS) { \
						printf(" %s", #NAME); \
					}
				EMIT_PIN_CONFIG
				#undef PIN
				printf("\n");
			} break;
			case COMMAND_subscribe_to_status: {
				is_subscribing_to_status = command_parser.arguments[0].b;
				printf(CPPP_DEBUG "status subscription = %d\n", is_subscribing_to_status);
			} break;
			default: {
				printf(CPPP_ERROR "unhandled command %s/%d\n",
					command_to_string(command_parser.command),
					command_parser.command);
			} break;
			}
		}
	}

	PANIC(PANIC_STOP);
}
