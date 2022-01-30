#include "spinlock.h"

#if defined (AARCH64)
spinlock_t fallbackLock;
#elif defined (X86)
volatile char lock_array[PADDED_ARRAY_SIZE_BYTES]
  __attribute__ ((aligned (CACHE_LINE_SIZE_BYTES))) ;
lockPtr_t locks;
#endif
