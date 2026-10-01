//===-- MMIX Linux pthread cleanup scopes ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_MMIX_PTHREAD_CLEANUP_H
#define LLVM_LIBC_MACROS_MMIX_PTHREAD_CLEANUP_H

struct __llvm_libc_mmix_cleanup_frame {
  void *owner;
  __UINTPTR_TYPE__ ro, procedure, sp, limit;
  __UINT64_TYPE__ chain, generation;
};
struct __llvm_libc_mmix_cleanup_record;
struct __llvm_libc_mmix_cleanup_link {
  struct __llvm_libc_mmix_cleanup_record *record;
  struct __llvm_libc_mmix_cleanup_frame frame;
  __UINT64_TYPE__ serial;
};
struct __llvm_libc_mmix_cleanup_record {
  struct __llvm_libc_mmix_cleanup_link previous;
  void (*callback)(void *);
  void *argument;
  __UINT64_TYPE__ serial;
};

#ifdef __cplusplus
extern "C" {
#endif
__attribute__((visibility("hidden"))) void
__llvm_libc_mmix_thread_cleanup_push(struct __llvm_libc_mmix_cleanup_record *,
                                     void (*)(void *), void *);
__attribute__((visibility("hidden"))) void
__llvm_libc_mmix_thread_cleanup_pop(struct __llvm_libc_mmix_cleanup_record *,
                                    int);
#ifdef __cplusplus
}

class __llvm_libc_mmix_cleanup_guard {
  void (*__callback)(void *);
  void *__argument;
  bool __active;

public:
  __llvm_libc_mmix_cleanup_guard(void (*__fn)(void *), void *__arg)
      : __callback(__fn), __argument(__arg), __active(true) {}
  __llvm_libc_mmix_cleanup_guard(const __llvm_libc_mmix_cleanup_guard &) =
      delete;
  __llvm_libc_mmix_cleanup_guard &
  operator=(const __llvm_libc_mmix_cleanup_guard &) = delete;
  ~__llvm_libc_mmix_cleanup_guard() noexcept { __finish(true); }
  void __finish(bool __execute) noexcept {
    bool __run = __active && __execute;
    __active = false;
    if (__run)
      __callback(__argument);
  }
};

#define pthread_cleanup_push(routine, arg)                                     \
  do {                                                                         \
    __llvm_libc_mmix_cleanup_guard __mmix_cleanup                              \
        __attribute__((annotate("mmix.pthread_cleanup_frame"))) ((routine),    \
                                                                 (arg));
#define pthread_cleanup_pop(execute)                                           \
  __mmix_cleanup.__finish((execute) != 0);                                     \
  }                                                                            \
  while (0)
#else
// The target compiler retains this C owner and its unwind tables under LTO.
#define pthread_cleanup_push(routine, arg)                                     \
  do {                                                                         \
    struct __llvm_libc_mmix_cleanup_record __mmix_cleanup                      \
        __attribute__((annotate("mmix.pthread_cleanup_frame")));               \
    __llvm_libc_mmix_thread_cleanup_push(&__mmix_cleanup, (routine), (arg));
#define pthread_cleanup_pop(execute)                                           \
  __llvm_libc_mmix_thread_cleanup_pop(&__mmix_cleanup, (execute));             \
  }                                                                            \
  while (0)
#endif

#endif // LLVM_LIBC_MACROS_MMIX_PTHREAD_CLEANUP_H
