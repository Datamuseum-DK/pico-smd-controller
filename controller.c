// blink codes for PANIC()
#define PANIC_XXX                       (0xAAA)
#define PANIC_STOP                      (0xA1)
#define PANIC_UNREACHABLE               (0xA3)

// deps
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
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
	// attempt to shut down things
	multicore_reset_core1();

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

unsigned core1_counter = 0;
void core1_entry() {
	for (;;) {
		core1_counter++;
		sleep_ms(1);
		tight_loop_contents();
	}
}

#define COMMANDS \
	CMD(stat,   ""    ) \
	CMD(led,    "u"   )

enum command {
	#define CMD(NAME, ARGFMT) CMD_ ## NAME,
	COMMANDS
	#undef CMD
	CMD_MAX
};

#define MAX_ARGS (4)
union arg {
	unsigned u;
};

const char* cmd2str(enum command cmd)
{
	switch (cmd) {
	#define CMD(NAME, ARGFMT) case CMD_ ## NAME: return #NAME;
	COMMANDS
	#undef CMD
	default: return "???";
	}
	PANIC(PANIC_UNREACHABLE);
}

static void cmd_stat(union arg* args)
{
	printf("STAT!\n");
}

static void cmd_led(union arg* args)
{
	int v = args[0].u != 0;
	printf("LED%d\n", v);
	set_led(v);
}

int main()
{
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 0);

	stdio_init_all();
	multicore_launch_core1(core1_entry);

	blink(50, 0); // "Hi, we're up!"

	unsigned stdin_received_bytes = 0;

	int token_index = 0;
	int token_buffer_cursor = 0;
	int line_error = 0;
	char token_buffer[1<<10];

	int cmd;
	char* argfmt;
	int argfmt_length;
	union arg args[MAX_ARGS];

	for (;;) {
		int got_char = getchar_timeout_us(0);
		if (got_char == PICO_ERROR_TIMEOUT || got_char == 0 || got_char >= 256) {
			tight_loop_contents();
			continue;
		}
		stdin_received_bytes++;

		char write_token_char = -1;
		int chk_token = 0;
		int chk_line = 0;
		if (' ' < got_char && got_char <= '~') {
			// token character
			write_token_char = got_char;
		} else if (got_char == ' ' || got_char == '\t') {
			// whitespace (separator)
			write_token_char = 0;
			chk_token = 1;
		} else if (got_char == '\r' || got_char == '\n') {
			// newline (end of command)
			write_token_char = 0;
			chk_token = chk_line = 1;
		}

		if (write_token_char >= 0) {
			if (token_buffer_cursor < sizeof(token_buffer)) {
				token_buffer[token_buffer_cursor++] = write_token_char;
			} else {
				printf("[ERR] token too long (exceeded %zd bytes)\n", sizeof(token_buffer));
				line_error = 1;
			}
		}

		int got_token = chk_token && strlen(token_buffer) > 0;
		int got_line = chk_line && token_index > 0;

		if (!line_error && got_token && token_buffer_cursor >= 2) {
			if (token_buffer[token_buffer_cursor-1] != 0) PANIC(PANIC_XXX);
			if (token_index == 0) {
				cmd = -1;
				argfmt = NULL;
				#define CMD(NAME, ARGFMT)                                       \
					if (cmd == -1 && strcmp(#NAME, token_buffer) == 0) {    \
						cmd = CMD_ ## NAME;                             \
						argfmt = ARGFMT;                                \
					}
				COMMANDS
				#undef CMD

				if (cmd == -1) {
					printf("[ERR] invalid command '%s'\n", token_buffer);
					line_error = 1;
				} else {
					argfmt_length = strlen(argfmt);
				}
			} else {
				const int arg_index = token_index-1;
				if (arg_index >= argfmt_length || arg_index >= MAX_ARGS) {
					printf("[ERR] too many arguments for command '%s' (expected %d)\n", cmd2str(cmd), argfmt_length);
					line_error = 1;
				} else {
					union arg* arg = &args[arg_index];
					switch (argfmt[arg_index]) {
					case 'u':
						arg->u = (unsigned)atol(token_buffer);
						break;
					default: PANIC(PANIC_UNREACHABLE);
					}
				}
			}
		}

		if (chk_token) {
			token_buffer_cursor = 0;
			token_index++;
		}

		if (got_line && !line_error) {
			const int n_args = token_index-1;
			if (n_args != argfmt_length) {
				printf("[ERR] too few arguments for command '%s' (expected %d; got %d)\n", cmd2str(cmd), argfmt_length, n_args);
			} else {
				switch (cmd) {
				#define CMD(NAME, ARGFMT) case CMD_ ## NAME: cmd_##NAME(args); break;
				COMMANDS
				#undef CMD
				default: PANIC(PANIC_UNREACHABLE);
				}
			}
		}

		if (chk_line) {
			token_buffer_cursor = 0;
			token_index = 0;
			argfmt = NULL;
		}
	}

	PANIC(PANIC_STOP);
}  
