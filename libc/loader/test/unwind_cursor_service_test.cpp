//===-- Composed cursor/image lease exhaustion tests -----------------------===//
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
  Input(ImageRole role, Address bias, bool frames = true)
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
    region = {0, view()};
    check(ElfImage::open_mapped({&region, 1}, 0, 64, role, bias, page,
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
  static Input image(ImageRole::MainPIE, 0x100000);
  const ImageRegistry::Image *images[] = {&image.record};
  ImageRegistry::Snapshot registry{1, images};
  static RuntimeScopes scopes(page);
  RuntimeScopes::Transaction startup(page);
  check(!scopes.prepare_startup(registry, startup));
  check(!scopes.prepare_unwind(startup) && !scopes.publish(startup));
  MMIXStartupUnwindService service{
      sizeof(service), 1, &scopes, forward_find, release_runtime_unwind_image,
      forward_acquire, forward_retain, drop_runtime_unwind_image,
      forward_lookup, forward_pin};
  check(__libunwind_mmix_adopt_startup_images(&service));
  lease_only = true;
  {
    int source, destination;
    _LIBUNWIND_CURSOR_SCOPE(source);
    _LIBUNWIND_CURSOR_SCOPE(destination);
    MMIXUnwindImage v{};
    check(__libunwind_mmix_acquire_image(0x102000, &v));
    source_lifetime.token = v.token;
    check(__libunwind_mmix_acquire_image(0x102000, &v));
    source_lifetime.fallback_token = v.token;
    check(__libunwind_mmix_acquire_image(0x102000, &v));
    destination_lifetime.token = v.token;
    const uintptr_t primary = source_lifetime.token;
    const uintptr_t fallback = source_lifetime.fallback_token;
    const uintptr_t original = destination_lifetime.token;
    UnwindImageView held[61]{};
    for (auto &view : held)
      check(!acquire_runtime_unwind_image(&scopes, 0x102000, &view));
    check(scopes.active_readers() == 64);
    for (unsigned free_slots = 0; free_slots != 2; ++free_slots) {
      retain_calls = 0;
      retain_error = 0;
      first_retained = 0;
      check(!__libunwind_mmix_cursor_copy(&destination, &source));
      check(retain_calls == free_slots + 1 && retain_error == EAGAIN);
      check(scopes.active_readers() == 64 - free_slots);
      check(source_lifetime.token == primary &&
            source_lifetime.fallback_token == fallback &&
            destination_lifetime.token == original &&
            !destination_lifetime.fallback_token);
      const uintptr_t original_tokens[] = {primary, fallback, original};
      for (uintptr_t token : original_tokens)
        check(__libunwind_mmix_lookup_image(token, 0x102000, &v));
      if (free_slots) {
        check(first_retained);
        UnwindImageView stale{};
        check(scopes.unwind_leases.lookup(first_retained, 0x102000, stale) ==
              EINVAL);
      }
      check(!drop_runtime_unwind_image(&scopes, held[free_slots].token));
    }
    retain_calls = 0;
    check(__libunwind_mmix_cursor_copy(&destination, &source));
    check(retain_calls == 2 && !retain_error && scopes.active_readers() == 63);
    check(!__libunwind_mmix_lookup_image(original, 0x102000, &v));
    check(destination_lifetime.token != primary &&
          destination_lifetime.fallback_token != fallback);
    __libunwind_mmix_cursor_reset(&source);
    check(scopes.active_readers() == 61);
    check(__libunwind_mmix_lookup_image(destination_lifetime.token, 0x102000, &v));
    check(__libunwind_mmix_lookup_image(destination_lifetime.fallback_token,
                                      0x102000, &v));
    __libunwind_mmix_cursor_reset(&destination);
    for (unsigned i = 2; i < 61; ++i)
      check(!drop_runtime_unwind_image(&scopes, held[i].token));
    check(!scopes.active_readers());
    UnwindImageView recovered[64]{};
    for (auto &view : recovered)
      check(!acquire_runtime_unwind_image(&scopes, 0x102000, &view));
    for (auto &view : recovered)
      check(!drop_runtime_unwind_image(&scopes, view.token));
    check(!scopes.active_readers());
  }
  lease_only = false;
  std::puts("composed cursor/image lease exhaustion and recovery passed");
  return 0;
}
