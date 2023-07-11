#ifndef COMMAND_PARSER_H

#include "controller_protocol.h"

enum command {
	#define COMMAND(NAME, ARGFMT) COMMAND_ ## NAME,
	EMIT_COMMANDS
	#undef COMMAND
};

#define COMMAND_MAX_ARGS (4)
union command_argument {
	unsigned b;
	unsigned u;
	int      i;
};

struct command_parser {
	int token_index;
	int token_buffer_cursor;
	int line_error;
	const char* argfmt;
	int argfmt_length;
	enum command command;
	union command_argument arguments[COMMAND_MAX_ARGS];
	char token_buffer[1<<10];
};

static inline const char* command_to_string(enum command command)
{
	switch (command) {
	#define COMMAND(NAME, ARGFMT) case COMMAND_ ## NAME: return #NAME;
	EMIT_COMMANDS
	#undef COMMAND
	}
	return "???";
}

int command_parser_put_char(struct command_parser*, int ch);

#define COMMAND_PARSER_H
#endif
