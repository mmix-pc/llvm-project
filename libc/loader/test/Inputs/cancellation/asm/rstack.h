// Native injection layout; target compilation uses the installed Linux UAPI.
#ifndef LLVM_LIBC_LOADER_TEST_RSTACK_H
#define LLVM_LIBC_LOADER_TEST_RSTACK_H
#include <stdint.h>
struct mmix_rstack_query {
  uint64_t chain_id, sigmask;
};
#define __NR_mmix_rstack_query 10000
#endif
