#pragma once
// CMSIS compiler definitions used by the pinned portable DAP sources on GCC/RISC-V.
#define __STATIC_INLINE static inline
#define __STATIC_FORCEINLINE static inline __attribute__((always_inline))
#define __WEAK __attribute__((weak))
#define __ASM __asm__
#define __NOP() __asm__ volatile ("nop")
