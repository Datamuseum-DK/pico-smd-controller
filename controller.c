// deps
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "tusb.h"

// local
#include "pin_config.h"
#include "base.h"
#include "command_parser.h"
#include "clocked_read.h"
#include "controller_protocol.h"
#include "xop.h"

unsigned stdin_received_bytes;
unsigned is_subscribing_to_status;
struct command_parser command_parser;
unsigned current_status;
absolute_time_t last_status_timestamp;
int is_job_polling;

static inline int gpio_type_to_dir(enum gpio_type t)
{
	switch (t) {
	case DATA:     return GPIO_IN;
	case STATUS:   return GPIO_IN;
	case CONTROL:  return GPIO_OUT;
	default: PANIC(PANIC_XXX);
	}
}

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
		if (status != current_status) {
			if (is_subscribing_to_status) {
				printf("%s %llu %d\n", CPPP_STATUS, t, status);
				last_status_timestamp = t;
			}
			current_status = status;
		}
	}

	if ((t - last_status_timestamp) > (1000000/60)) {
		if (is_subscribing_to_status) {
			printf("%s %llu\n", CPPP_STATUS_TIME, t);
		}
		last_status_timestamp = t;
	}
}

static void handle_frontend_data_transfers(void)
{
	int written_buffer_index = get_written_buffer_index();
	if (written_buffer_index < 0) return;
	// XXX TODO transfer buffer; base64 encoded probably? not
	// everything in one go; have some define that limits transfers
	unsigned buffer_size = get_buffer_size(written_buffer_index);
	printf(CPPP_DEBUG "TODO transfer buffer %d / sz=%d\n", written_buffer_index, buffer_size);
	release_buffer(written_buffer_index);
}

static void handle_job_status(void)
{
	if (!is_job_polling) return;
	enum xop_status st = poll_xop_status();
	if (st == XST_DONE) {
		printf(CPPP_INFO "Job OK!\n");
		is_job_polling = 0;
	} else if (st >= XST_ERR0) {
		printf(CPPP_INFO "Job FAILED! (error:%d)\n", st);
		is_job_polling = 0;
	}
}

static void parse(void)
{
	int got_char = getchar_timeout_us(0);
	if (got_char == PICO_ERROR_TIMEOUT || got_char == 0 || got_char >= 256) {
		return;
	}

	stdin_received_bytes++;
	if (!command_parser_put_char(&command_parser, got_char)) {
		return;
	}

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
	case COMMAND_set_ctrl: {
		unsigned ctrl = command_parser.arguments[0].u;
		#define PUT(NAME) \
			{ \
				const unsigned mask = 1 << CONTROL_ ## NAME; \
				gpio_put(GPIO_ ## NAME, ctrl & mask); \
				ctrl = ctrl & ~mask; \
			}
		PUT(UNIT_SELECT_TAG)
		PUT(TAG1)
		PUT(TAG2)
		PUT(TAG3)
		PUT(BIT0)
		PUT(BIT1)
		PUT(BIT2)
		PUT(BIT3)
		PUT(BIT4)
		PUT(BIT5)
		PUT(BIT6)
		PUT(BIT7)
		PUT(BIT8)
		PUT(BIT9)
		#undef PUT
		if (ctrl != 0) {
			printf(CPPP_WARNING "unsupported remaining ctrl pins: %x", ctrl);
		}
	} break;
	case COMMAND_terminate_op: {
		terminate_op();
	} break;
	case COMMAND_op_blink_test: {
		int fail = command_parser.arguments[0].u;
		xop_blink_test(fail);
		is_job_polling = 1;
	} break;
	case COMMAND_op_raw_tag: {
		const unsigned tag      = command_parser.arguments[0].u;
		const unsigned argument = command_parser.arguments[1].u;
		xop_raw_tag(tag, argument);
		is_job_polling = 1;
	} break;
	case COMMAND_op_rtz: {
		xop_rtz();
		is_job_polling = 1;
	} break;
	case COMMAND_op_select_unit0: {
		xop_select_unit0();
		is_job_polling = 1;
	} break;
	case COMMAND_op_select_cylinder: {
		xop_select_cylinder(command_parser.arguments[0].u);
		is_job_polling = 1;
	} break;
	case COMMAND_op_select_head: {
		xop_select_head(command_parser.arguments[0].u);
		is_job_polling = 1;
	} break;
	case COMMAND_op_read_enable: {
		xop_read_enable(command_parser.arguments[0].i);
		is_job_polling = 1;
	} break;
	case COMMAND_op_read_data: {
		if (!can_allocate_buffer()) {
			printf(CPPP_ERROR "no buffer available\n");
		} else {
			unsigned buffer_index = xop_read_data(
				command_parser.arguments[0].u,
				command_parser.arguments[1].u,
				command_parser.arguments[2].u);
			is_job_polling = 1;
			printf(CPPP_DEBUG "reading into buffer %d\n", buffer_index);
		}
	} break;
	default: {
		printf(CPPP_ERROR "unhandled command %s/%d\n",
			command_to_string(command_parser.command),
			command_parser.command);
	} break;
	}
}

int main()
{
	// I/O pin config
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 0);
	#define PIN(TYPE, NAME, GPN)                                       \
		gpio_init(GPN);                                            \
		gpio_set_dir(GPN, gpio_type_to_dir(TYPE));                 \
		if (gpio_type_to_dir(TYPE) == GPIO_OUT) {                  \
			gpio_put(GPN, 0);                                  \
		} else {                                                   \
			gpio_pull_down(GPN); /* prevent floating inputs */ \
		}
	EMIT_PIN_CONFIG
	#undef PIN

	clocked_read_init();

	stdio_init_all();

	blink(50, 0); // "Hi, we're up!"

	for (;;) {
		parse();
		status_housekeeping();
		handle_frontend_data_transfers();
		handle_job_status();
		//tight_loop_contents(); // does nothing
		tud_task();
	}

	PANIC(PANIC_STOP);
}
