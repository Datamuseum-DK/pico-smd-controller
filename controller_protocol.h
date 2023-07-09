#ifndef CONTROLLER_PROTOCOL_H

//              name                    argfmt
#define EMIT_COMMANDS \
	COMMAND(get_status_descriptors, ""    ) \
	COMMAND(subscribe,              "u"   ) \
	COMMAND(led,                    "u"   )
// argfmt:
//   u: unsigned 32bit

// controller protocol prefixes: response from controller should begin with one
// of these
#define CTPP_STATUS_DESCRIPTORS "DS:"
#define CTPP_LOG "["
#define CTPP_ERROR CTPP_LOG"ERROR] "

#define CONTROLLER_PROTOCOL_H
#endif
