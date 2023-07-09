#ifndef BASE_H

#include <stdint.h>

// blink codes for PANIC()
#define PANIC_XXX                       (0xAAA)
#define PANIC_STOP                      (0xA1)
#define PANIC_UNREACHABLE               (0xA3)
#define PANIC_UNHANDLED_COMMAND         (0xAC)

#define ARRAY_LENGTH(xs) (sizeof(xs) / sizeof(xs[0]))
#define ARRAY_END(xs) ((xs) + ARRAY_LENGTH(xs))
//#define LOG2_ALIGN(LG2, VALUE) (((VALUE) + ((1<<LG2)-1)) & (~((1<<LG2)-1)))

#define  LED_PIN  PICO_DEFAULT_LED_PIN // GP25

void set_led(int p);
void blink(int on_ms, int off_ms);

__attribute__ ((noreturn))
void PANIC(uint32_t error);

#define BASE_H
#endif
