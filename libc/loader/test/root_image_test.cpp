//===-- Resident root metadata preparation and query tests ----------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "../../../libunwind/src/mmix/CursorLifetime.h"
#include <cstdio>
#include <cstdlib>

static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    std::fprintf(stderr, "root image check at %d\n", line);
    std::abort();
  }
}
alignas(8) static unsigned char metadata[128];
static MMIXUnwindReadableRange range;
static MMIXUnwindImage image;
static unsigned acquires, drops, pins;
static bool fail_acquire, fail_pin;
static uintptr_t live_token;
static void put(unsigned offset, unsigned width, uint64_t value) {
  for (unsigned i = 0; i != width; ++i)
    metadata[offset + i] = value >> (8 * (width - i - 1));
}
static long acquire(void *, uintptr_t, MMIXUnwindImage *out) {
  ++acquires;
  *out = {};
  if (fail_acquire)
    return 11;
  check(!live_token);
  live_token = acquires;
  *out = image;
  out->token = live_token;
  return 0;
}
static long drop(void *, uintptr_t token) {
  check(token && token == live_token);
  ++drops;
  live_token = 0;
  return 0;
}
static long pin(void *, uintptr_t token) {
  check(token && token == live_token);
  ++pins;
  if (fail_pin)
    return 11;
  live_token = 0;
  return 0;
}
static long unused_query(void *, uintptr_t, MMIXUnwindImage *) {
  std::abort();
}
static long unused_lookup(void *, uintptr_t, uintptr_t, MMIXUnwindImage *) {
  std::abort();
}
static void unused_release(void *, uintptr_t) { std::abort(); }

extern "C" int main(int argc, char **) {
  MMIXUnwindImage out{};
  check(!__libunwind_mmix_prepare_root_image(0x102000));
  check(!__libunwind_mmix_find_root_image(0x102000, &out));
  range = {reinterpret_cast<uintptr_t>(metadata), sizeof(metadata)};
  image = {0x100000, reinterpret_cast<uintptr_t>(metadata), 1,
           reinterpret_cast<uintptr_t>(metadata + 64), 16,
           reinterpret_cast<uintptr_t>(&range), 1, 0};
  put(0, 4, 1); // PT_LOAD
  put(4, 4, 5); // PF_R | PF_X
  put(16, 8, 0x2000);
  put(32, 8, 0x100);
  put(40, 8, 0x100);
  MMIXStartupUnwindService service{sizeof(service), 1, &image, unused_query,
                                  unused_release, acquire, unused_query, drop,
                                  unused_lookup, pin};
  if (argc > 1) {
    service.ownership = {acquire, [](void *, uintptr_t) -> long { std::abort(); },
                         drop, unused_lookup, pin};
    auto partial = service;
    partial.ownership.retain = nullptr;
    check(!__libunwind_mmix_adopt_startup_images(&partial));
  }
  check(__libunwind_mmix_adopt_startup_images(&service));
  fail_acquire = true;
  check(!__libunwind_mmix_prepare_root_image(0x102000));
  check(acquires == 1 && drops == 0 && pins == 0);
  fail_acquire = false;
  image.eh_frame_header_size = 11;
  check(!__libunwind_mmix_prepare_root_image(0x102000));
  check(drops == 1 && pins == 0 && !live_token);
  image.eh_frame_header_size = 16;
  check(!__libunwind_mmix_prepare_root_image(0x102100));
  check(drops == 2 && pins == 0);
  put(4, 4, 4); // Readable data is not a root activation.
  check(!__libunwind_mmix_prepare_root_image(0x102000));
  put(4, 4, 5);
  put(40, 8, UINT64_MAX);
  check(!__libunwind_mmix_prepare_root_image(0x102000));
  put(40, 8, 0x80); // filesz exceeds memsz.
  check(!__libunwind_mmix_prepare_root_image(0x102000));
  put(40, 8, 0x100);
  fail_pin = true;
  check(!__libunwind_mmix_prepare_root_image(0x102000));
  check(drops == 6 && pins == 1 && !live_token);
  check(!__libunwind_mmix_find_root_image(0x102000, &out));
  fail_pin = false;
  check(__libunwind_mmix_prepare_root_image(0x102000));
  check(!live_token && pins == 2 && drops == 6);
  unsigned before = acquires;
  fail_acquire = true; // Ordinary lease capacity is now unavailable.
  check(!__libunwind_mmix_prepare_root_image(0x102000));
  for (unsigned i = 0; i != 1000; ++i) {
    check(__libunwind_mmix_find_root_image(0x102000 + i % 256, &out));
    check(out.phdr_address == image.phdr_address && out.token);
  }
  const uintptr_t misses[] = {0, 0x101fff, 0x102100, 0x202000, UINTPTR_MAX};
  for (uintptr_t pc : misses) {
    check(!__libunwind_mmix_find_root_image(pc, &out));
    check(!out.token && !out.phdr_address);
  }
  check(!__libunwind_mmix_find_root_image(0x102000, nullptr));
  check(acquires == before && pins == 2 && drops == 6);
  MMIXCursorCheckpoint baseline{};
  __libunwind_mmix_cursor_checkpoint(&baseline);
  int root_cursor, ordinary_cursor, exception;
  MMIXCursorLifetime root{}, ordinary{};
  __libunwind_mmix_cursor_begin_root(&root, &root_cursor);
  __libunwind_mmix_cursor_begin(&ordinary, &ordinary_cursor);
  check(!__libunwind_mmix_cursor_copy(&ordinary_cursor, &root_cursor));
  check(!__libunwind_mmix_cursor_copy(&root_cursor, &ordinary_cursor));
  __libunwind_mmix_cursor_end(&ordinary);
  __libunwind_mmix_set_managed_stop(1234);
  check(!__libunwind_mmix_forced_enter(&root_cursor, &exception, 1234, nullptr));
  for (unsigned i = 0; i != 2; ++i) {
    {
      MMIXCursorQuery query(&root_cursor);
      bool owned = false;
      check(mmixFindCursorImage(0x102000, out, owned) && owned);
      check(!mmixFindCursorImage(0x202000, out, owned) && owned);
      check(!root.token && !root.fallback_token);
    }
    __libunwind_mmix_cursor_reset(&root_cursor);
  }
  check(acquires == before && pins == 2 && drops == 6);
  __libunwind_mmix_cursor_end(&root);
  check(__libunwind_mmix_cursor_at_checkpoint(&baseline));
  __libunwind_mmix_cursor_begin(&ordinary, &ordinary_cursor);
  {
    MMIXCursorQuery query(&ordinary_cursor);
    bool owned = false;
    check(!mmixFindCursorImage(0x102000, out, owned) && owned);
  }
  check(acquires == before + 1 && !ordinary.token);
  __libunwind_mmix_cursor_end(&ordinary);
  check(__libunwind_mmix_cursor_at_checkpoint(&baseline));
  std::puts("root image preparation, rollback and capacity-independent queries passed");
  return 0;
}
