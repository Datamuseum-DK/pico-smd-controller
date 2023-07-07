#ifndef LOG_H

#include <stdint.h>

__attribute__ ((format (printf, 1, 2)))
int log_printf(const char* fmt, ...);

#define DBGF(...)   log_printf("DEBUG: " __VA_ARGS__)
#define INFOF(...)  log_printf("INFO: "  __VA_ARGS__)

#define LOG_H
#endif
