//===-- Resident MMIX startup image service -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "StartupImages.h"

namespace {
MMIXStartupUnwindService service{};
}

extern "C" __attribute__((visibility("protected"))) bool
__libunwind_mmix_adopt_startup_images(const MMIXStartupUnwindService *input) {
  if (service.find || !input || input->size != sizeof(*input) ||
      input->process_resident != 1 || !input->context || !input->find ||
      !input->release)
    return false;
  service = *input;
  return true;
}

extern "C" bool __libunwind_mmix_find_startup_image(uintptr_t pc,
                                                    MMIXUnwindImage *view) {
  if (!view)
    return false;
  *view = {};
  return service.find && service.find(service.context, pc, view) == 0;
}

extern "C" void __libunwind_mmix_release_startup_image(uintptr_t token) {
  if (token && service.release)
    service.release(service.context, token);
}
