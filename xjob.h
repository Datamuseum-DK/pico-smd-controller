#ifndef XJOB_H

// core1 (second CPU core) on RP2040 is used to run "jobs".

typedef unsigned xjob;

xjob xjob_select_unit0(void);
xjob xjob_cancel(void);

#define XJOB_H
#endif
