#ifndef XJOB_H

// core1 (second CPU core) on RP2040 is reserved for executing "jobs".

#include "controller_protocol.h"

enum xop_status {
	XST_RUNNING               = 0,
	XST_DONE                  = 1,
	XST_ERR0                  = 1000,
	XST_ERR_DRIVE_ERROR       = 1001,
	XST_ERR_DRIVE_NOT_READY   = 1002,
	XST_ERR_TIMEOUT           = 1999,
	XST_ERR_TEST              = 2001,
};

enum xop_status poll_xop_status(void);
void terminate_op(void);

void xop_blink_test(int fail);
void xop_raw_tag(enum tag tag, unsigned argument);
void xop_rtz(void);
void xop_select_unit0(void);
void xop_select_cylinder(unsigned cylinder);
void xop_select_head(unsigned head);
void xop_read_enable(int servo_offset, int data_strobe_delay);
unsigned xop_read_data(unsigned n_32bit_words, unsigned index_sync, unsigned raw);
void xop_read_batch(unsigned cylinder0, unsigned cylinder1, unsigned head_set, unsigned n_32bit_words_per_track, int servo_offset, int data_strobe_delay);

#define XJOB_H
#endif
