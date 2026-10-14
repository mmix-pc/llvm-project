//===-- Resident MMIX startup image service ----------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_MMIX_STARTUP_IMAGES_H
#define LIBUNWIND_MMIX_STARTUP_IMAGES_H

#include <stdint.h>

// Private same-toolchain interface. The owner serializes adoption before
// callbacks or workers and keeps the service and every image alive until exit.
// This service cannot be used with unloadable images.
struct MMIXUnwindReadableRange {
  uint64_t address;
  uint64_t size;
};
struct MMIXUnwindImage {
  uint64_t load_bias;
  uint64_t phdr_address;
  uint64_t phdr_count;
  uint64_t eh_frame_header;
  uint64_t eh_frame_header_size;
  uint64_t readable_ranges;
  uint64_t range_count;
  uint64_t token;
};
struct MMIXStartupUnwindService {
  uint64_t size;
  uint64_t process_resident;
  void *context;
  long (*find)(void *, uintptr_t, MMIXUnwindImage *);
  void (*release)(void *, uintptr_t);
};

extern "C" bool
__libunwind_mmix_adopt_startup_images(const MMIXStartupUnwindService *);
extern "C" __attribute__((visibility("hidden"))) bool
__libunwind_mmix_find_startup_image(uintptr_t, MMIXUnwindImage *);
extern "C" __attribute__((visibility("hidden"))) void
__libunwind_mmix_release_startup_image(uintptr_t);

#endif
