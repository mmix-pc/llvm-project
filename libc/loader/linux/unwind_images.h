//===-- Startup unwind images ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_UNWIND_IMAGES_H
#define LLVM_LIBC_LOADER_LINUX_UNWIND_IMAGES_H
#include "loader/linux/arena.h"
#include "loader/linux/elf.h"
#include "src/__support/OSUtil/linux/loader_services.h"
namespace LIBC_NAMESPACE_DECL {
namespace loader {
class StartupUnwindImages {
  struct Record {
    const ElfImage *image;
    UnwindImageView view;
  };
  cpp::span<const Record> records;
  bool published = false;

public:
  StartupUnwindImages() = default;
  StartupUnwindImages(const StartupUnwindImages &) = delete;
  StartupUnwindImages &operator=(const StartupUnwindImages &) = delete;
  // Positive errno. Caller serializes preparation and owns unpublished arena
  // rollback. Images, metadata and arena must be immutable and process-resident
  // after publication. This is one immutable set; runtime publication builds
  // a replacement set. No unload or reference-counted leases are provided.
  long prepare(cpp::span<const ElfImage *const> images, Arena &arena);
  bool publish();
  bool is_published() const { return published; }
  long find(Address pc, UnwindImageView &out) const;
  bool owns(Address token) const;
};
long find_unwind_image(void *context, uintptr_t pc, UnwindImageView *view);
void release_unwind_image(void *context, uintptr_t token);
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
