//===-- Root cache with real snapshot ownership tests -----------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "dynamic_fixture.h"
#include "loader/linux/runtime_unwind.h"
#include "../../../libunwind/src/CursorLifetime.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <cstring>
#include <unistd.h>

using namespace loader_test;
static bool lease_only;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    std::fprintf(stderr, "cursor service check at %d\n", line);
    std::abort();
  }
}
namespace LIBC_NAMESPACE::loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  check(!lease_only);
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
[[noreturn]] void fatal_startup() { _exit(127); }
}
struct Input : DynamicFixture {
  MappedRegion region;
  ImageRegistry::Image record{};
  void *mapping = nullptr;
  size_t mapping_size = sizeof(bytes) + page;
  Address address = 0;
  Input(ImageRole role, bool frames = true)
      : DynamicFixture(role) {
    put(56, 2, 9);
    segment(0, PT_PHDR, PF_R, 64, 64, 9 * 56, 9 * 56, 8);
    if (frames) {
      segment(8, PT_GNU_EH_FRAME, PF_R, 0xc00, 0xc00, 20, 20, 4);
      put(0xc00, 4, 0x011b033b);
      put(0xc04, 4, 0xd00 - 0xc04);
      put(0xc08, 4, 1);
      put(0xc0c, 4, 0x2000 - 0xc00);
      put(0xc10, 4, 0xd00 - 0xc00);
    }
    // Real readable backing below the MMIX user-address limit lets the
    // production cache inspect program headers without rewriting its views.
    mapping = ::mmap(nullptr, mapping_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    check(mapping != MAP_FAILED);
    address = (reinterpret_cast<Address>(mapping) + page - 1) & ~(page - 1);
    std::memcpy(reinterpret_cast<void *>(address), bytes, sizeof(bytes));
    region = {0, Bytes(reinterpret_cast<const unsigned char *>(address),
                       sizeof(bytes))};
    check(ElfImage::open_mapped({&region, 1}, 0, 64, role, address, page,
                                record.elf) == ElfError::None);
    check(DynamicInfo::open(record.elf, record.dynamic) == DynamicError::None);
    record.discovered = true;
    record.startup = role == ImageRole::MainPIE;
  }
};

// Field-wise forwarding avoids aliasing two distinct private ABI structs.
static void transfer(const UnwindImageView &v, MMIXUnwindImage *out) {
  *out = {v.load_bias, v.phdr_address, v.phdr_count, v.eh_frame_header,
          v.eh_frame_header_size, v.readable_ranges, v.range_count, v.token};
}
static long forward_find(void *p, uintptr_t pc, MMIXUnwindImage *out) {
  UnwindImageView v{};
  long error = find_runtime_unwind_image(p, pc, &v);
  transfer(v, out);
  return error;
}
static long forward_acquire(void *p, uintptr_t pc, MMIXUnwindImage *out) {
  UnwindImageView v{};
  long error = acquire_runtime_unwind_image(p, pc, &v);
  transfer(v, out);
  return error;
}
static unsigned retain_calls;
static long retain_error;
static uintptr_t first_retained;
static long forward_retain(void *p, uintptr_t token, MMIXUnwindImage *out) {
  UnwindImageView v{};
  long error = retain_runtime_unwind_image(p, token, &v);
  if (++retain_calls == 1 && !error)
    first_retained = v.token;
  retain_error = error;
  transfer(v, out);
  return error;
}
static long forward_lookup(void *p, uintptr_t token, uintptr_t pc,
                           MMIXUnwindImage *out) {
  UnwindImageView v{};
  long error = static_cast<RuntimeScopes *>(p)->unwind_leases.lookup(token, pc, v);
  transfer(v, out);
  return error;
}
static long forward_pin(void *p, uintptr_t token) {
  return static_cast<RuntimeScopes *>(p)->unwind_leases.pin(token);
}
extern "C" int main() {
  static Input image(ImageRole::MainPIE), late(ImageRole::SharedObject);
  const uintptr_t root_pc = image.address + 0x2000;
  const uintptr_t late_pc = late.address + 0x2000;
  const ImageRegistry::Image *images[] = {&image.record, &late.record};
  ImageRegistry::Snapshot initial{1, {images, 1}}, registry{2, images};
  static RuntimeScopes scopes(page);
  RuntimeScopes::Transaction startup(page);
  check(!scopes.prepare_startup(initial, startup));
  check(!scopes.prepare_unwind(startup) && !scopes.publish(startup));
  MMIXStartupUnwindService service{
      sizeof(service), 1, &scopes, forward_find, release_runtime_unwind_image,
      forward_acquire, forward_retain, drop_runtime_unwind_image,
      forward_lookup, forward_pin};
  check(__libunwind_mmix_adopt_startup_images(&service));
  lease_only = true;
  check(__libunwind_mmix_prepare_root_image(root_pc));
  check(!scopes.active_readers());
  lease_only = false;

  RuntimeScopes::Transaction open(page), close(page);
  uint64_t handle;
  check(!scopes.prepare_open(registry, &late.record, false, open, handle));
  check(!scopes.prepare_unwind(open) && !scopes.publish(open));
  check(!scopes.prepare_close(handle, close));
  check(!scopes.prepare_unwind(close) && !scopes.publish(close));
  UnwindImageView held[64]{}, refused{};
  lease_only = true;
  for (auto &view : held)
    check(!acquire_runtime_unwind_image(&scopes, late_pc, &view));
  check(acquire_runtime_unwind_image(&scopes, late_pc, &refused) == EAGAIN);
  check(!refused.token && scopes.active_readers() == 64);
  int root_cursor, ordinary_cursor;
  MMIXCursorLifetime root{}, ordinary{};
  __libunwind_mmix_cursor_begin_root(&root, &root_cursor);
  MMIXUnwindImage view{};
  {
    MMIXCursorQuery query(&root_cursor);
    bool owned = false;
    for (unsigned i = 0; i != 1000; ++i)
      check(mmixFindCursorImage(root_pc, view, owned) && owned);
    check(!mmixFindCursorImage(late_pc, view, owned));
  }
  __libunwind_mmix_cursor_end(&root);
  check(scopes.active_readers() == 64);
  __libunwind_mmix_cursor_begin(&ordinary, &ordinary_cursor);
  {
    MMIXCursorQuery query(&ordinary_cursor);
    bool owned = false;
    check(!mmixFindCursorImage(late_pc, view, owned) && owned);
  }
  __libunwind_mmix_cursor_end(&ordinary);
  check(scopes.active_readers() == 64);
  lease_only = false;

  RuntimeScopes::Transaction retire(page);
  const ImageRegistry::Image *removed[] = {&late.record};
  check(!scopes.prepare_retire(removed, retire));
  check(!scopes.prepare_unwind(retire) && !scopes.publish(retire));
  check(!scopes.readers_drained(&late.record));
  check(scopes.collect_snapshots() == -EBUSY);
  lease_only = true;
  check(acquire_runtime_unwind_image(&scopes, late_pc, &refused) == ENOENT);
  for (auto &owner : held) {
    check(!scopes.unwind_leases.lookup(owner.token, late_pc, refused));
    check(!drop_runtime_unwind_image(&scopes, owner.token));
  }
  check(!scopes.active_readers());
  lease_only = false;
  check(scopes.readers_drained(&late.record));
  check(!scopes.collect_snapshots());
  check(!::munmap(late.mapping, late.mapping_size));
  late.mapping = nullptr;
  lease_only = true;
  check(__libunwind_mmix_find_root_image(root_pc, &view));
  check(!__libunwind_mmix_find_root_image(late_pc, &view));
  check(!scopes.active_readers());
  lease_only = false;
  std::puts("real lease exhaustion preserves root queries and late-image drain");
  return 0;
}
