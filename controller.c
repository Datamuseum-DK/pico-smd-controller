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
#include "base64.h"
#include "adler32.h"
#include "dbgclk.pio.h"

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
	case DBGCLK:   return GPIO_OUT;
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

#define DATA_TRANSFER_BYTES_PER_LINE (60)
_Static_assert((DATA_TRANSFER_BYTES_PER_LINE % 3) == 0, "must be divisible by 3 (to make base-64 encoding easier)");
#define DATA_TRANSFER_CHARACTERS_PER_LINE ((DATA_TRANSFER_BYTES_PER_LINE/3)*4)
#define DATA_TRANSFER_LINES_PER_CHUNK (10)

struct {
	int is_transfering;
	unsigned buffer_index;
	unsigned bytes_transferred;
	unsigned bytes_total;
	unsigned sequence;
	struct adler32 adler;
} data_transfer;

static void handle_frontend_data_transfers(void)
{
	if (!data_transfer.is_transfering) {
		int buffer_index = get_written_buffer_index();
		if (buffer_index < 0) {
			// nothing to transfer
			return;
		}

		memset(&data_transfer, 0, sizeof data_transfer);
		data_transfer.is_transfering = 1;
		data_transfer.buffer_index = buffer_index;
		data_transfer.bytes_total = get_buffer_size(buffer_index);
		printf("%s %d %s\n", CPPP_DATA_HEADER, data_transfer.bytes_total, get_buffer_filename(buffer_index));
		adler32_init(&data_transfer.adler);
	}

	if (!data_transfer.is_transfering) PANIC(PANIC_XXX);
	for (int i = 0; i < DATA_TRANSFER_LINES_PER_CHUNK && data_transfer.is_transfering; i++) {
		const int remaining = data_transfer.bytes_total - data_transfer.bytes_transferred;
		const int n = remaining > DATA_TRANSFER_BYTES_PER_LINE ? DATA_TRANSFER_BYTES_PER_LINE : remaining;

		char line[DATA_TRANSFER_CHARACTERS_PER_LINE+20];
		char* wp = line;
		int offset = snprintf(line, sizeof line, "%s %.05d ", CPPP_DATA_LINE, data_transfer.sequence++);
		if (offset <= 0) PANIC(PANIC_XXX);
		wp += offset;
		const int buffer_index = data_transfer.buffer_index;

		uint8_t* data = get_buffer_data(buffer_index) + data_transfer.bytes_transferred;
		adler32_push(&data_transfer.adler, data, n);
		wp = base64_encode(wp, data, n);
		*(wp++) = '\n';
		*(wp++) = 0;
		puts(line);
		data_transfer.bytes_transferred += n;
		if (data_transfer.bytes_transferred == data_transfer.bytes_total) {
			data_transfer.is_transfering = 0;
			release_buffer(buffer_index);
			uint32_t checksum = adler32_sum(&data_transfer.adler);
			printf("%s %.05d %lu\n", CPPP_DATA_FOOTER, data_transfer.sequence, checksum);
			break;
		} else if (data_transfer.bytes_transferred > data_transfer.bytes_total) {
			PANIC(PANIC_XXX);
		}
	}
}

static void handle_job_status(void)
{
	if (!is_job_polling) return;
	enum xop_status st = poll_xop_status();
	if (st == XST_DONE) {
		printf(CPPP_INFO "Job OK! (took %llu microseconds)\n", xop_duration_us());
		is_job_polling = 0;
	} else if (st >= XST_ERR0) {
		printf(CPPP_INFO "Job FAILED! (error:%d, took %llu microseconds)\n", st, xop_duration_us());
		is_job_polling = 0;
	}
}

static void job_begin(void)
{
	is_job_polling = 1;
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
	case COMMAND_poll_gpio: {
		printf(CPPP_INFO " GPIO %lx\n", gpio_get_all() & ~0x1000000);
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
	case COMMAND_xfer_test: {
		if (!can_allocate_buffer()) {
			printf(CPPP_ERROR "no buffer available\n");
		} else {
			unsigned size = command_parser.arguments[0].u;
			const unsigned buffer_index = allocate_buffer(size);
			char* s = get_buffer_filename(buffer_index);
			snprintf(s, CLOCKED_READ_BUFFER_FILENAME_MAX_LENGTH, "_xfertest-bufidx%d-%dbytes.garbage", buffer_index, size);
			size = get_buffer_size(buffer_index); // may be truncated
			uint8_t* p = get_buffer_data(buffer_index);
			for (unsigned i = 0; i < size; i++) {
				*(p++) = (i & 0xff) + ((i >> 8) & 0xff) + ((i >> 16) & 0xff) + ((i >> 24) & 0xff);
			}
			wrote_buffer(buffer_index);
		}
	} break;
	case COMMAND_terminate_op: {
		terminate_op();
		printf(CPPP_INFO "TERMINATE!\n");
	} break;
	case COMMAND_op_blink_test: {
		int fail = command_parser.arguments[0].u;
		job_begin();
		xop_blink_test(fail);
	} break;
	case COMMAND_op_raw_tag: {
		const unsigned tag      = command_parser.arguments[0].u;
		const unsigned argument = command_parser.arguments[1].u;
		job_begin();
		xop_raw_tag(tag, argument);
	} break;
	case COMMAND_op_rtz: {
		job_begin();
		xop_rtz();
	} break;
	case COMMAND_op_select_unit0: {
		job_begin();
		xop_select_unit0();
	} break;
	case COMMAND_op_select_cylinder: {
		job_begin();
		xop_select_cylinder(command_parser.arguments[0].u);
	} break;
	case COMMAND_op_select_head: {
		job_begin();
		xop_select_head(command_parser.arguments[0].u);
	} break;
	case COMMAND_op_read_enable: {
		int servo_offset = command_parser.arguments[0].i;
		int data_strobe_delay = command_parser.arguments[1].i;
		job_begin();
		xop_read_enable(servo_offset, data_strobe_delay);
	} break;
	case COMMAND_op_read_data: {
		if (!can_allocate_buffer()) {
			printf(CPPP_ERROR "no buffer available\n");
		} else {
			job_begin();
			unsigned buffer_index = xop_read_data(
				command_parser.arguments[0].u,
				command_parser.arguments[1].u,
				command_parser.arguments[2].u);
			printf(CPPP_DEBUG "reading into buffer %d\n", buffer_index);
		}
	} break;
	case COMMAND_op_read_batch: {
		const unsigned cylinder0      = command_parser.arguments[0].u;
		const unsigned cylinder1      = command_parser.arguments[1].u;
		const unsigned head_set       = command_parser.arguments[2].u;
		const unsigned n_32bit_words  = command_parser.arguments[3].u;
		const int servo_offset        = command_parser.arguments[4].i;
		const int data_strobe_delay   = command_parser.arguments[5].i;
		job_begin();
		xop_read_batch(cylinder0, cylinder1, head_set, n_32bit_words, servo_offset, data_strobe_delay);
	} break;
	default: {
		printf(CPPP_ERROR "unhandled command %s/%d\n",
			command_to_string(command_parser.command),
			command_parser.command);
	} break;
	}
}

static void dbgclk_start(void)
{
	const PIO pio = pio1;
	const uint offset = pio_add_program(pio, &dbgclk_program);
	const uint sm = pio_claim_unused_sm(pio, true);
	pio_sm_config cfg = dbgclk_program_get_default_config(0);
	const unsigned gpio_pin = GPIO_DEBUGCLK_10MHZ;
	pio_gpio_init(pio, gpio_pin);
	sm_config_set_set_pins(&cfg, gpio_pin, 1);
	//pio_sm_set_consecutive_pindirs(pio, sm, gpio_pin, /*pin_count=*/1, /*is_out=*/true);
	sm_config_set_clkdiv_int_frac(&cfg, 3, 0); // XXX trying to aim for ~10MHz
	pio_sm_init(pio, sm, offset, &cfg);
	pio_sm_set_enabled(pio, sm, true);
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
	dbgclk_start();

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
