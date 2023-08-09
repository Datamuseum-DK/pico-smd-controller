static inline void loopback_test_generate_data(unsigned char* dst, int n_bytes)
{
	for (int i = 0; i < n_bytes; i++) {
		unsigned char x = 0;
		switch (i&3) {
		case 0: x = 0x00; break; // 0b00000000
		case 1: x = 0xaa; break; // 0b10101010
		case 2: x = 0xff; break; // 0b11111111
		case 3: x = 0x55; break; // 0b01010101
		}
		dst[i] = ((i>>2)&0xff) ^ x;
	}
}
