#ifndef CONTROLLER_PROTOCOL_H

// BRIEF
// Commands are in the form "<verb> [arg0] [arg1]...\n". Verbs and argfmt
// (argument format) are defined by EMIT_COMMANDS below. "argfmt" is a string
// with one char for each argument:
//   - b: boolean (unsigned/uint32_t)
//   - u: unsigned/uint32_t
// The controller sends messages:
//   - if it begins with "[" it's a log message
//   - otherwise it's a payload; see CTPP_ (controller protocol prefix) below

//              name                    argfmt
#define EMIT_COMMANDS \
	COMMAND(get_status_descriptors, ""    ) \
	COMMAND(subscribe_to_status,    "b"   ) \
	COMMAND(led,                    "b"   )

// controller protocol prefixes: response from controller should begin with one
// of these
#define CTPP_STATUS_DESCRIPTORS "DS"
#define CTPP_STATUS             "ST"
#define CTPP_STATUS_TIME        "S0"
#define CTPP_LOG "["
#define CTPP_ERROR CTPP_LOG"ERROR] "
#define CTPP_DEBUG CTPP_LOG"DEBUG] "

#define CONTROLLER_PROTOCOL_H
#endif
