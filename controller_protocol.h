#ifndef CONTROLLER_PROTOCOL_H

// BRIEF
// Commands are in the form "<verb> [arg0] [arg1]...\n". Verbs and argfmt
// (argument format) are defined by EMIT_COMMANDS below. "argfmt" is a string
// with one char for each argument:
//   - b: boolean (unsigned/uint32_t)
//   - u: unsigned/uint32_t
// The controller sends messages:
//   - if it begins with "[" it's a log message
//   - otherwise it's a payload; see CPPP_* below

//              name                      argfmt
#define EMIT_COMMANDS \
	COMMAND(get_status_descriptors,   ""    ) \
	COMMAND(subscribe_to_status,      "b"   ) \
	COMMAND(led,                      "b"   ) \
	COMMAND(op_cancel,                ""    ) \
	COMMAND(op_select_unit0,          ""    ) \
	COMMAND(op_select_cylinder,       "u"   ) \
	COMMAND(op_select_head,           "u"   )

// controller protocol payload prefixes: response from controller should begin
// with one of these
#define CPPP_STATUS_DESCRIPTORS "DS"
#define CPPP_STATUS             "ST"
#define CPPP_STATUS_TIME        "S0"
#define CPPP_LOG "["
#define CPPP_ERROR CPPP_LOG"ERROR] "
#define CPPP_DEBUG CPPP_LOG"DEBUG] "

#define CONTROLLER_PROTOCOL_H
#endif
