//===-- MMIX image ownership service -----------------------------*- C++
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

// Private same-toolchain interface, adopted before callbacks or workers.
// Checked leases keep immutable image extents alive through explicit release.
// A raw view copy is a borrow; retain creates a separate owner. Operations on
// the same owner must not race its release. Resident queries remain a temporary
// safety boundary for cursors without tracked copy/destruction lifetimes.
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
// Allocation-free references used only by registered private owners. A copied
// nonzero handle is a borrow until retain succeeds; drop consumes one owner.
// Handles are not checked tokens and may not originate in application storage.
struct MMIXUnwindOwnership {
  long (*acquire)(void *, uintptr_t, MMIXUnwindImage *);
  long (*retain)(void *, uintptr_t);
  long (*drop)(void *, uintptr_t);
  long (*lookup)(void *, uintptr_t, uintptr_t, MMIXUnwindImage *);
  long (*pin)(void *, uintptr_t);
};
struct MMIXStartupUnwindService {
  uint64_t size;
  // Applies only to the transitional find/release query, not checked leases.
  uint64_t process_resident;
  void *context;
  long (*find)(void *, uintptr_t, MMIXUnwindImage *);
  void (*release)(void *, uintptr_t);
  long (*acquire)(void *, uintptr_t, MMIXUnwindImage *);
  long (*retain)(void *, uintptr_t, MMIXUnwindImage *);
  long (*drop)(void *, uintptr_t);
  long (*lookup)(void *, uintptr_t, uintptr_t, MMIXUnwindImage *);
  long (*pin)(void *, uintptr_t);
  MMIXUnwindOwnership ownership{};
};

extern "C" bool
__libunwind_mmix_adopt_startup_images(const MMIXStartupUnwindService *);
extern "C" __attribute__((visibility("hidden"))) bool
__libunwind_mmix_find_startup_image(uintptr_t, MMIXUnwindImage *);
extern "C" __attribute__((visibility("hidden"))) void
__libunwind_mmix_release_startup_image(uintptr_t);
extern "C" __attribute__((visibility("hidden"))) bool
__libunwind_mmix_acquire_image(uintptr_t, MMIXUnwindImage *);
extern "C" __attribute__((visibility("hidden"))) bool
__libunwind_mmix_retain_image(uintptr_t, MMIXUnwindImage *);
extern "C" __attribute__((visibility("hidden"))) bool
__libunwind_mmix_drop_image(uintptr_t);
extern "C" __attribute__((visibility("hidden"))) bool
__libunwind_mmix_lookup_image(uintptr_t, uintptr_t, MMIXUnwindImage *);
extern "C" __attribute__((visibility("hidden"))) bool
__libunwind_mmix_pin_image(uintptr_t);

// Prepare once during single-threaded adoption, before application callbacks.
// The caller must identify a mandatory root activation in a startup provider.
// Successful preparation permanently owns that startup snapshot; subsequent
// root-only queries neither acquire finite leases nor change ownership.
extern "C" bool __libunwind_mmix_prepare_root_image(uintptr_t);
extern "C" __attribute__((visibility("hidden"))) bool
__libunwind_mmix_find_root_image(uintptr_t, MMIXUnwindImage *);

#endif
