#ifndef SPN_GUEST_H
#define SPN_GUEST_H

#ifndef SP_SP_H
  #include <stdint.h>

  typedef int8_t   s8;
  typedef int16_t  s16;
  typedef int32_t  s32;
  typedef int64_t  s64;
  typedef uint8_t  u8;
  typedef uint16_t u16;
  typedef uint32_t u32;
  typedef uint64_t u64;
  typedef float    f32;
  typedef double   f64;
  typedef char     c8;
#endif

#include "spn/err.h"

typedef enum {
  SPN_OS_NONE,
  SPN_OS_WINDOWS,
  SPN_OS_LINUX,
  SPN_OS_MACOS,
  SPN_OS_WASI,
  SPN_OS_FREESTANDING,
} spn_os_t;

typedef enum {
  SPN_MODE_NONE,
  SPN_MODE_DEBUG,
  SPN_MODE_RELEASE,
} spn_mode_t;

typedef enum {
  SPN_OPT_LEVEL_NONE,
  SPN_OPT_LEVEL_0,
  SPN_OPT_LEVEL_1,
  SPN_OPT_LEVEL_2,
  SPN_OPT_LEVEL_3,
  SPN_OPT_LEVEL_S,
  SPN_OPT_LEVEL_Z,
} spn_opt_level_t;

typedef enum {
  SPN_SANITIZER_NONE      = 0,
  SPN_SANITIZER_ADDRESS   = 1 << 0,
  SPN_SANITIZER_THREAD    = 1 << 1,
  SPN_SANITIZER_UNDEFINED = 1 << 2,
  SPN_SANITIZER_MEMORY    = 1 << 3,
  SPN_SANITIZER_LEAK      = 1 << 4,
} spn_sanitizer_t;

typedef u32 spn_sanitizer_set_t;

#endif
