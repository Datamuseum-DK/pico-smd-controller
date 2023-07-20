#ifndef B64_H

char* base64_encode(char* output, uint8_t* input, int n_bytes);
uint8_t* base64_decode_line(uint8_t* output, char* line);

#define B64_H
#endif
