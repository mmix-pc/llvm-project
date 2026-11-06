//===-- Timer/image native test transport interface ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_TEST_TIMER_IMAGE_TEST_H
#define LLVM_LIBC_LOADER_TEST_TIMER_IMAGE_TEST_H

// Plain transport boundary: the full-build timer types stay in their own TU.
struct TimerImageTestAPI {
  void *context;
  long (*retain)(void *, __UINTPTR_TYPE__, __UINT64_TYPE__ *);
  long (*release)(void *, __UINT64_TYPE__);
  void (*observe)(void *, unsigned);
};
extern "C" void check_timer_image_composition(TimerImageTestAPI,
                                              __UINTPTR_TYPE__);
extern "C" void *timer_image_allocate(__SIZE_TYPE__);
extern "C" void timer_image_free(void *);
#endif
