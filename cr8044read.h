#ifndef CR8044READ_H

// The CR8044 controller writes sectors like this:

//   Gap-A            31 bytes
//   Address field     9 bytes
//   Gap-B            19 bytes
//   Data field      551 bytes
//   EOS               1 byte
//   Gap-C          17.5 bytes
//   -------------------------
//   Total         628.5 bytes

// All "gaps" consists of zeroes.

// Both address and data fields start with a SYNC byte (10111001).

// Gap-A comes immediately after a SECTOR pulse, but in practice the timing of
// the address SYNC varies by ~60 bits.

// The CR8044 controller (most likely) only wrote the data field when writing
// data to a sector, starting the write-stream somewhere in Gap-B.

// In practice, if we attempt to read a sector by reading 628.5 bytes after the
// SECTOR pulse, the data signal is often "inverted" ~30 bits into Gap-B. This
// "inversion point" (most likely) corresponds to the point where the CR8044
// controller starts writing new data to a sector.

// The CDC9762 manual states that reads should be enabled (TAG3+BIT1) at a time
// when the drive can read 7.75µs worth of zeroes (~75 bits).

// So, the attempted solution for reading is:
//  after INDEX pulse:
//  - enable read
//  - wait for READ_DATA to go high (first bit in SYNC byte)
//  - read 9 bytes (address field), clocked by READ_CLOCK
//  - disable read
//  - wait M bits, clocked by SERVO_CLOCK
//  - enable read
//  - read 511+1 bytes (data field), clocked by READ_CLOCK
//  - disable read
//  - wait for SECTOR
//  - repeat

// M should be somewhere between 30 and 77. 30 is the "inversion point", and
// past 77 the drive no longer has 75 zero bits for sync.

// XXX should match controller_protocol.h
#define CR8044READ_ADDRESS_SIZE (9)
#define CR8044READ_DATA_SIZE (551+1)
#define CR8044READ_BYTES_PER_SECTOR (CR8044READ_ADDRESS_SIZE+CR8044READ_DATA_SIZE)
#define CR8044READ_N_SECTORS (32) // XXX read a little more
#define CR8044READ_BYTES_TOTAL (CR8044READ_N_SECTORS * CR8044READ_BYTES_PER_SECTOR)

#include <stdint.h>
#include "hardware/pio.h"

void cr8044read_init(PIO _pio, uint _dma_channel, uint _dma_channel2);
void cr8044read_execute(uint8_t* dst);

#define CR8044READ_H
#endif
