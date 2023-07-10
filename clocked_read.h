#ifndef CLOCKED_READ_H

#include <stdint.h>

// TABLE 1-1 in SMD/CDC docs:
// Data Capacity               40 MB             80 MB
//   Bytes/Track                20 160            20 160
//   Bytes/Cylinder            100 800           100 800
//   Bytes/Spindle          41 428 800        82 958 400
//   Cylinders/Spindle             411               823
#define BYTES_PER_TRACK (20160)

// triple buffering, 3.05 revolutions per buffer
// 20160*3.05   =  61488 bytes (15372 32-bit words)
// 20160*3.05*3 = 184464 bytes
#define CLOCKED_READ_BUFFER_COUNT   3
#define CLOCKED_READ_BUFFER_SIZE    ((3*BYTES_PER_TRACK) + (BYTES_PER_TRACK/20))
// NOTE: I don't think triple buffering really makes a difference, but it might
// with improved downlink speeds (raw USB instead of USB/TTY?):
//   USB/TTY speed:   ~500kB/s; ~375kB/s after base64 enc; 3Mbit/s
//   Drive:            NRZ signal at 9.67MHz
//   1 Revolution:     16.67ms (3600RPM)

void clocked_read_init(void);
void clocked_read_into_buffer(unsigned buffer_index);
uint8_t* clocked_read_get_buffer(unsigned buffer_index);

#define CLOCKED_READ_H
#endif
