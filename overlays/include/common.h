#ifndef AOE2_OVERLAYS_COMMON_H
#define AOE2_OVERLAYS_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef offsetof
#define offsetof(st, m) ((unsigned int)((char *)&((st *)0)->m - (char *)0))
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint32_t GuestAddr;

#endif /* AOE2_OVERLAYS_COMMON_H */
