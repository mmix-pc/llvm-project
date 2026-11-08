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
MMIXUnwindImage root_image{};
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

// The service describes MMIX ELF64 big-endian program headers. Its validated
// readable ranges cover the complete table while the acquired owner is live.
static uint64_t root_word(const unsigned char *p, unsigned width) {
  uint64_t result = 0;
  for (unsigned i = 0; i != width; ++i)
    result = (result << 8) | p[i];
  return result;
}

static bool root_contains(const MMIXUnwindImage &view, uintptr_t pc) {
  if (!view.token || pc < view.load_bias)
    return false;
  const auto *headers =
      reinterpret_cast<const unsigned char *>(view.phdr_address);
  bool found = false;
  for (uint64_t i = 0; i != view.phdr_count; ++i) {
    const auto *p = headers + i * 56;
    if (root_word(p, 4) != 1) // PT_LOAD
      continue;
    uint64_t address = root_word(p + 16, 8);
    uint64_t files = root_word(p + 32, 8);
    uint64_t size = root_word(p + 40, 8);
    if (files > size || address > UINTPTR_MAX - view.load_bias ||
        size > UINTPTR_MAX - view.load_bias - address)
      return false;
    if ((root_word(p + 4, 4) & 5) == 5 &&
        pc - view.load_bias >= address &&
        pc - view.load_bias - address < size)
      found = true;
  }
  return found;
}

extern "C" __attribute__((visibility("protected"))) bool
__libunwind_mmix_prepare_root_image(uintptr_t pc) {
  if (root_image.token)
    return false;
  MMIXUnwindImage view{};
  if (!__libunwind_mmix_acquire_image(pc, &view))
    return false;
  if (!root_contains(view, pc) || !__libunwind_mmix_pin_image(view.token)) {
    if (!__libunwind_mmix_drop_image(view.token))
      __builtin_trap();
    return false;
  }
  // The token has been consumed as a permanent pin. It is only a nonzero
  // publication marker in this private cache, never a releasable lease.
  root_image = view;
  return true;
}

extern "C" bool __libunwind_mmix_find_root_image(uintptr_t pc,
                                                MMIXUnwindImage *out) {
  if (!out)
    return false;
  *out = {};
  if (!root_contains(root_image, pc))
    return false;
  *out = root_image;
  return true;
}
