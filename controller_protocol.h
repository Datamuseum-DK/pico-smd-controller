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
	COMMAND(get_status_descriptors,   ""         ) \
	COMMAND(subscribe_to_status,      "b"        ) \
	COMMAND(poll_gpio,                ""         ) \
	COMMAND(set_ctrl,                 "u"        ) \
	COMMAND(led,                      "b"        ) \
	COMMAND(terminate_op,             ""         ) \
	COMMAND(op_blink_test,            "u"        ) \
	COMMAND(op_raw_tag,               "uu"       ) \
	COMMAND(op_select_unit0,          ""         ) \
	COMMAND(op_rtz,                   ""         ) \
	COMMAND(op_select_cylinder,       "u"        ) \
	COMMAND(op_select_head,           "u"        ) \
	COMMAND(op_read_enable,           "ii"       ) \
	COMMAND(op_read_data,             "uuu"      ) \
	COMMAND(op_read_batch,            "uuuuii"   )

// controller protocol payload prefixes: response from controller should begin
// with one of these
#define CPPP_STATUS_DESCRIPTORS "DS"
#define CPPP_STATUS             "ST"
#define CPPP_STATUS_TIME        "S0"
#define CPPP_LOG "["
#define CPPP_ERROR   CPPP_LOG"ERROR] "
#define CPPP_WARNING CPPP_LOG"WARNING] "
#define CPPP_INFO    CPPP_LOG"INFO] "
#define CPPP_DEBUG   CPPP_LOG"DEBUG] "

#define EMIT_CONTROLS                     \
	CONTROL(UNIT_SELECT_TAG,     1)  \
	CONTROL(UNIT_SELECT_BIT0,    0)  \
	CONTROL(UNIT_SELECT_BIT1,    0)  \
	CONTROL(UNIT_SELECT_BIT2,    0)  \
	CONTROL(UNIT_SELECT_BIT3,    0)  \
	CONTROL(TAG1,                1)  \
	CONTROL(TAG2,                1)  \
	CONTROL(TAG3,                1)  \
	CONTROL(BIT0,                1)  \
	CONTROL(BIT1,                1)  \
	CONTROL(BIT2,                1)  \
	CONTROL(BIT3,                1)  \
	CONTROL(BIT4,                1)  \
	CONTROL(BIT5,                1)  \
	CONTROL(BIT6,                1)  \
	CONTROL(BIT7,                1)  \
	CONTROL(BIT8,                1)  \
	CONTROL(BIT9,                1)  \
	CONTROL(OPEN_CABLE_DETECTOR, 0)

enum ctrl {
	#define CONTROL(NAME,SUPPORTED) CONTROL_ ## NAME,
	EMIT_CONTROLS
	#undef CONTROL
	CONTROL_MAX
};
#ifndef __cplusplus
_Static_assert(CONTROL_MAX <= 32, "must fit in 32-bit bitmask");
#endif

enum tag {
	TAG_UNIT_SELECT = 0,
	TAG1            = 1,
	TAG2            = 2,
	TAG3            = 3,
	TAG_CLEAR       = 999,
};

#define EMIT_TAG3_BITS                                                                    \
	BIT(WRITE_GATE,             "Enable write"                                      ) \
	BIT(READ_GATE,              "Enable read"                                       ) \
	BIT(SERVO_OFFSET_POSITIVE,  "Servo Offset Positive (250µin towards spindle)"    ) \
	BIT(SERVO_OFFSET_NEGATIVE,  "Servo Offset Negative (250µin away from spindle)"  ) \
	BIT(FAULT_CLEAR,            "Controller Fault Clear"                            ) \
	BIT(ADDRESS_MARK_ENABLE,    "Enable Address Mark R/W"                           ) \
	BIT(RTZ_SEEK,               "Return to Track Zero, Clear Error"                 ) \
	BIT(DATA_STROBE_EARLY,      "Data Strobe Early (PLO data separator)"            ) \
	BIT(DATA_STROBE_LATE,       "Date Strobe Late (PLO data separator)"             ) \
	BIT(RELEASE,                "Release (dual-channel only)"                       )

enum tag3pos {
	#define BIT(NAME,DESC) TAG3POS_ ## NAME,
	EMIT_TAG3_BITS
	#undef BIT
};

enum tag3bit {
	#define BIT(NAME,DESC) TAG3BIT_ ## NAME = (1 << TAG3POS_ ## NAME),
	EMIT_TAG3_BITS
	#undef BIT
};


#define CONTROLLER_PROTOCOL_H
#endif
