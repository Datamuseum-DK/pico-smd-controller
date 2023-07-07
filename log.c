#include <stdarg.h>

// Configure stb_sprintf.h to write at most one character before calling our
// user callback that handles ring buffer wrap around conditions. It might be
// possible to write bigger chunks at a time, but: I don't plan on logging A
// LOT, and; the RP2040 has a lot of processing power but not a lot of RAM,
// and; we would sometimes have to add padding at the end of the ringbuffer,
// and possibly in the middle of a message, due to the way stb_sprintf.h works,
// and I don't think it's worth it.
#define STB_SPRINTF_MIN 1
#define STB_SPRINTF_NOFLOAT
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"

#include "log.h"

uint32_t log_read_cursor;
uint32_t log_write_cursor;
char log_ringbuf[1<<13];
#define RINGBUF_INDEX_MASK (sizeof(log_ringbuf)-1)

inline __attribute__((always_inline))
static char* log_callback(char const* buf, void* usr, int len)
{
	return (char*) buf + (((buf-log_ringbuf) + len) & RINGBUF_INDEX_MASK);
}

int log_printf(const char* fmt, ...)
{
	char* bufp = &log_ringbuf[log_write_cursor & RINGBUF_INDEX_MASK];
	va_list args;
	va_start(args, fmt);
	int n = stbsp_vsprintfcb(log_callback, NULL, bufp, fmt, args);
	log_write_cursor += n;
	va_end(args);
	return n;
}
