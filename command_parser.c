#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "base.h"
#include "command_parser.h"

static void reset_parser(struct command_parser* parser)
{
	parser->token_index = 0;
	parser->token_buffer_cursor = 0;
	parser->line_error = 0;
}

int command_parser_put_char(struct command_parser* parser, int ch)
{
	int write_token_char = -1;

	int end_of_token = 0;
	int end_of_line = 0;
	if (' ' < ch && ch <= '~') {
		// token character
		write_token_char = ch;
	} else if (ch == ' ' || ch == '\t') {
		// whitespace (separator)
		write_token_char = 0;
		end_of_token = 1;
	} else if (ch == '\r' || ch == '\n') {
		// newline (end of command)
		write_token_char = 0;
		end_of_token = end_of_line = 1;
	}

	if (write_token_char == -1) return 0;

	if (!parser->line_error && write_token_char >= 0) {
		if (parser->token_buffer_cursor < sizeof(parser->token_buffer)) {
			parser->token_buffer[parser->token_buffer_cursor++] = write_token_char;
		} else {
			printf(CTPP_ERROR "token too long (exceeded %zd bytes)\n", sizeof(parser->token_buffer));
			parser->line_error = 1;
		}
	}

	int got_token = end_of_token && strlen(parser->token_buffer) > 0;

	if (!parser->line_error && got_token) {
		// assert zero-terminated token buffer
		if (parser->token_buffer[parser->token_buffer_cursor-1] != 0) PANIC(PANIC_XXX);

		if (parser->token_index == 0) {
			int found_command = 0;
			#define COMMAND(NAME, ARGFMT)                                              \
				if (!found_command && strcmp(#NAME, parser->token_buffer) == 0) {  \
					found_command = 1;                                         \
					parser->command = COMMAND_ ## NAME;                        \
					parser->argfmt = ARGFMT;                                   \
				}
			EMIT_COMMANDS
			#undef COMMAND

			if (!found_command) {
				printf(CTPP_ERROR "invalid command '%s'\n", parser->token_buffer);
				parser->line_error = 1;
			} else {
				parser->argfmt_length = strlen(parser->argfmt);
			}
		} else {
			const int arg_index = parser->token_index-1;
			if (arg_index >= parser->argfmt_length || arg_index >= COMMAND_MAX_ARGS) {
				printf(CTPP_ERROR "too many arguments for command '%s' (expected %d)\n", command_to_string(parser->command), parser->argfmt_length);
				parser->line_error = 1;
			} else {
				union command_argument* arg = &parser->arguments[arg_index];
				switch (parser->argfmt[arg_index]) {
				case 'u':
					arg->u = (unsigned)atol(parser->token_buffer);
					break;
				default: PANIC(PANIC_UNREACHABLE);
				}
			}
		}
		parser->token_index++;
	}

	int got_line = end_of_line && parser->token_index > 0;

	if (!parser->line_error && got_line) {
		const int n_args = parser->token_index-1;
		if (n_args != parser->argfmt_length) {
			printf(CTPP_ERROR "too few arguments for command '%s' (expected %d; got %d)\n", command_to_string(parser->command), parser->argfmt_length, n_args);
		} else {
			reset_parser(parser);
			return 1;
		}
	}

	if (end_of_token) parser->token_buffer_cursor = 0;
	if (end_of_line) reset_parser(parser);

	return 0;
}

// -----------------------------------------------------------------------------------------
// cc -DUNIT_TEST command_parser.c -o unittest_command_parser && ./unittest_command_parser
#ifdef UNIT_TEST

#include <stdio.h>
#include <assert.h>

struct command_parser command_parser;

static void try_parse(const char* line, enum command expected_command)
{
	int got_command = 0;
	const size_t n = strlen(line);
	for (int i = 0; i < n; i++) {
		char ch = line[i];
		if (command_parser_put_char(&command_parser, ch)) {
			assert(!got_command && "got command twice?!");
			if (command_parser.command != expected_command) {
				fprintf(stderr, "expected command %d, got %d\n", expected_command, command_parser.command);
				abort();
			}
			got_command = 1;
		}
	}
	assert(got_command && "expected to get command");
}

int main(int argc, char** argv)
{
	reset_parser(&command_parser);
	try_parse("get_status_descriptors\r\n", COMMAND_get_status_descriptors);
	try_parse("led 0\n", COMMAND_led);
	try_parse("led 1\n", COMMAND_led);
	try_parse("\n\r\nled 0\n\n\n", COMMAND_led);
	try_parse("led   \t 555\n", COMMAND_led);
	assert(command_parser.arguments[0].u == 555);
	try_parse(" led    666  \n", COMMAND_led);
	assert(command_parser.arguments[0].u == 666);
	try_parse("led 420\n\n", COMMAND_led);
	try_parse("get_status_descriptors\n", COMMAND_get_status_descriptors);
	assert(command_parser.arguments[0].u == 420);
	try_parse("subscribe 424242\n", COMMAND_subscribe);
	assert(command_parser.arguments[0].u == 424242);
	printf("OK\n");
}

void PANIC(uint32_t error)
{
	fprintf(stderr, "PANIC! (CODE 0x%X)\n", error);
	abort();
}

#endif
