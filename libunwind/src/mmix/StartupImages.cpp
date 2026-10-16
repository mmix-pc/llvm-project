//===-- MMIX image ownership service --------------------------------------===//
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
      !input->release || !input->acquire || !input->retain || !input->drop ||
      !input->lookup || !input->pin)
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

static bool valid_image(const MMIXUnwindImage &view) {
  bool invalid =
      !view.token || !view.phdr_address || view.phdr_address % 8 ||
      !view.phdr_count || view.phdr_count > UINT16_MAX ||
      !view.readable_ranges || view.readable_ranges % 8 || !view.range_count ||
      view.range_count > UINT16_MAX || view.eh_frame_header_size < 12 ||
      view.eh_frame_header_size > UINT32_MAX ||
      view.eh_frame_header > UINTPTR_MAX - view.eh_frame_header_size ||
      view.phdr_address > UINTPTR_MAX - view.phdr_count * 56 ||
      view.readable_ranges > UINTPTR_MAX - view.range_count * 16;
  if (!invalid) {
    // Range storage belongs to the checked service and remains leased here.
    const auto *ranges =
        reinterpret_cast<const MMIXUnwindReadableRange *>(view.readable_ranges);
    bool headers = false, index = false;
    for (uint64_t i = 0; i < view.range_count; ++i) {
      const auto &range = ranges[i];
      if (!range.size || range.address > UINTPTR_MAX - range.size) {
        invalid = true;
        break;
      }
      auto contains = [&](uintptr_t address, uintptr_t size) {
        return address >= range.address && size <= range.size &&
               address - range.address <= range.size - size;
      };
      headers |= contains(view.phdr_address, view.phdr_count * 56);
      index |= contains(view.eh_frame_header, view.eh_frame_header_size);
    }
    invalid |= !headers || !index;
  }
  return !invalid;
}

static bool checked_image(long (*query)(void *, uintptr_t, MMIXUnwindImage *),
                          uintptr_t key, MMIXUnwindImage *out) {
  if (!out)
    return false;
  *out = {};
  if (!query)
    return false;
  MMIXUnwindImage view{};
  if (query(service.context, key, &view))
    return false;
  if (!valid_image(view)) {
    if (view.token && service.drop(service.context, view.token))
      __builtin_trap();
    return false;
  }
  *out = view;
  return true;
}

extern "C" bool __libunwind_mmix_acquire_image(uintptr_t pc,
                                               MMIXUnwindImage *out) {
  return checked_image(service.acquire, pc, out);
}
extern "C" bool __libunwind_mmix_retain_image(uintptr_t token,
                                              MMIXUnwindImage *out) {
  return checked_image(service.retain, token, out);
}
extern "C" bool __libunwind_mmix_drop_image(uintptr_t token) {
  return token && service.drop && !service.drop(service.context, token);
}
extern "C" bool __libunwind_mmix_lookup_image(uintptr_t token, uintptr_t pc,
                                              MMIXUnwindImage *out) {
  if (!out)
    return false;
  *out = {};
  MMIXUnwindImage view{};
  if (!service.lookup || service.lookup(service.context, token, pc, &view) ||
      view.token != token || !valid_image(view))
    return false;
  *out = view;
  return true;
}
extern "C" bool __libunwind_mmix_pin_image(uintptr_t token) {
  return token && service.pin && !service.pin(service.context, token);
}
