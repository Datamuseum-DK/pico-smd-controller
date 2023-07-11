#ifndef CLOCKED_READ_H

#include <stdint.h>

#include "drive.h"

// triple buffering, 3.05 revolutions per buffer
// 20160*3.05   =  61488 bytes (15372 32-bit words)
// 20160*3.05*3 = 184464 bytes
#define CLOCKED_READ_BUFFER_COUNT   3
#define CLOCKED_READ_BUFFER_SIZE    ((3*DRIVE_BYTES_PER_TRACK) + (DRIVE_BYTES_PER_TRACK/20))
// NOTE: I don't think triple buffering really makes a difference, but it might
// with improved downlink speeds (raw USB instead of USB/TTY?):
//   USB/TTY speed:   ~500kB/s; ~375kB/s after base64 enc; 3Mbit/s
//   Drive:            NRZ signal at 9.67MHz
//   1 Revolution:     16.67ms (3600RPM)

void clocked_read_init(void);
void clocked_read_into_buffer(unsigned buffer_index, unsigned word_32bit_count);
int clocked_read_is_running(void);

enum buffer_status {
	FREE,
	BUSY, // "allocated" or "writing"
	WRITTEN,
};

unsigned can_allocate_buffer(void);
unsigned allocate_buffer(unsigned size);
uint8_t* get_buffer_data(unsigned buffer_index);
enum buffer_status get_buffer_status(unsigned buffer_index);
void release_buffer(unsigned buffer_index);
void wrote_buffer(unsigned buffer_index);
int get_written_buffer_index(void);
unsigned get_buffer_size(unsigned buffer_index);

#define CLOCKED_READ_H
#endif
