#ifndef XJOB_H

// core1 (second CPU core) on RP2040 is reserved for executing "jobs".

typedef unsigned xjob;

enum xjob_status {
	XST_RUNNING           = 0,
	XST_OK                = 1,
	XST_ERR0              = 1000,
	XST_ERR_DRIVE_ERROR   = 1001,
	XST_ERR_TIMEOUT       = 1002,
};

int get_xjob_status(xjob job_id);

xjob xjob_select_unit0(void);
xjob xjob_select_cylinder(unsigned cylinder);
xjob xjob_select_head(unsigned head);
xjob xjob_rtz(void);
xjob xjob_cancel(void);

#define XJOB_H
#endif
