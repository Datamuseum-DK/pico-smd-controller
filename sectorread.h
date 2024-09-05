#ifndef SECTORREAD_H

#include <stdint.h>
#include "hardware/pio.h"

struct segment {
	int wait_bits;
	int data_bits;
};
void sectorread_init(PIO _pio, uint _dma_channel, uint _dma_channel2, int n_segments, struct segment* segments);
void sectorread_execute(uint8_t* dst);

#define SECTORREAD_H
#endif
