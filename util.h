#ifndef COMMON_H

#define ARRAY_LENGTH(xs) (sizeof(xs) / sizeof(xs[0]))
#define ARRAY_END(xs) ((xs) + ARRAY_LENGTH(xs))

//#define LOG2_ALIGN(LG2, VALUE) (((VALUE) + ((1<<LG2)-1)) & (~((1<<LG2)-1)))

#define COMMON_H
#endif
