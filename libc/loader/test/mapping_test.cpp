//===-- Loader DSO mapping and rollback tests -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/mapping.h"
#include "loader/linux/syscall.h"
#include "loader/linux/target.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace loader_test;

// A non-PIE host test keeps dereferenceable storage inside the MMIX user range.
// This model checks ownership/protection requests, not real kernel enforcement.
alignas(page) static unsigned char pool[4 * 1024 * 1024];
struct Allocation {
  Address address, size;
  bool reservation, live;
};
static Allocation allocations[64];
static unsigned used, allocation_count, calls, fail_at, close_count;
static unsigned reservations, fixed_maps, injected;
static bool fd_open, fail_unmap, corrupt_mapping, interrupt_read;
static const DynamicFixture *source;
static size_t position, actual_size, advertised_size, read_limit;
static int protection[sizeof(pool) / page];

static unsigned live_count() {
  unsigned count = 0;
  for (unsigned i = 0; i < allocation_count; ++i)
    count += allocations[i].live;
  return count;
}

static void start(const DynamicFixture &f) {
  assert(!fd_open && !live_count());
  used = allocation_count = calls = fail_at = close_count = 0;
  reservations = fixed_maps = injected = 0;
  fail_unmap = corrupt_mapping = interrupt_read = false;
  source = &f;
  position = 0;
  actual_size = advertised_size = read_limit = sizeof(f.bytes);
  memset(pool, 0xa5, sizeof(pool));
  memset(protection, 0, sizeof(protection));
}

static Allocation *containing(Address address, Address size) {
  for (unsigned i = 0; i < allocation_count; ++i) {
    auto &a = allocations[i];
    if (a.live && address >= a.address && size <= a.size &&
        address - a.address <= a.size - size)
      return &a;
  }
  return nullptr;
}

namespace LIBC_NAMESPACE_DECL {
namespace loader {
long raw_syscall(long number, long a, long b, long c, long d, long e, long f) {
  ++calls;
  // Linux consumes close's fd even on a reported error.
  if (number == SYS_close) {
    assert(fd_open && a == 7);
    fd_open = false;
    ++close_count;
  }
  if (calls == fail_at) {
    ++injected;
    return -EIO;
  }
  if (number == SYS_openat) {
    assert(!fd_open);
    fd_open = true;
    return 7;
  }
  if (number == SYS_close)
    return 0;
  if (number == SYS_lseek) {
    assert(fd_open && a == 7 && b == 0);
    position = c == SEEK_END ? advertised_size : 0;
    return position;
  }
  if (number == SYS_read) {
    assert(fd_open && a == 7);
    if (interrupt_read) {
      interrupt_read = false;
      return -EINTR;
    }
    size_t size = actual_size - position;
    if (size > size_t(c))
      size = c;
    if (size > read_limit)
      size = read_limit;
    memcpy(reinterpret_cast<void *>(b), source->bytes + position, size);
    position += size;
    return size;
  }
  if (number == SYS_mmap) {
    assert(b > 0 && !(b & (page - 1)) && (d & MAP_PRIVATE));
    assert((c & (PROT_WRITE | PROT_EXEC)) != (PROT_WRITE | PROT_EXEC));
    Address address;
    if (d & MAP_FIXED) {
      auto *owner = containing(a, b);
      assert(owner && owner->reservation && !(a & (page - 1)));
      address = a;
      ++fixed_maps;
    } else {
      assert(d & MAP_ANONYMOUS);
      assert(used + b <= sizeof(pool) && allocation_count < 64);
      address = reinterpret_cast<Address>(pool + used);
      used += b;
      allocations[allocation_count++] = {address, Address(b), c == PROT_NONE,
                                         true};
      reservations += c == PROT_NONE;
    }
    if (d & MAP_ANONYMOUS) {
      assert(e == -1);
      memset(reinterpret_cast<void *>(address), 0, b);
    } else {
      assert(fd_open && e == 7 && f >= 0 && !(f & (page - 1)));
      assert(size_t(f) < actual_size);
      size_t size = actual_size - f;
      if (size > size_t(b))
        size = b;
      memcpy(reinterpret_cast<void *>(address), source->bytes + f, size);
      memset(reinterpret_cast<void *>(address + size), 0, b - size);
      if (corrupt_mapping && f == 0)
        *reinterpret_cast<unsigned char *>(address) = 0;
    }
    for (Address i = 0; i < Address(b); i += page)
      protection[(address + i - reinterpret_cast<Address>(pool)) / page] = c;
    return address;
  }
  if (number == SYS_mprotect) {
    auto *owner = containing(a, b);
    assert(owner && owner->reservation);
    assert((c & (PROT_WRITE | PROT_EXEC)) != (PROT_WRITE | PROT_EXEC));
    for (Address i = 0; i < Address(b); i += page)
      protection[(a + i - reinterpret_cast<Address>(pool)) / page] = c;
    return 0;
  }
  if (number == SYS_munmap) {
    auto *owner = containing(a, b);
    assert(owner && owner->address == Address(a) && owner->size == Address(b));
    if (fail_unmap)
      return -EIO;
    owner->live = false;
    return 0;
  }
  assert(false && "unexpected loader syscall");
  return -ENOSYS;
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL

struct SparseFixture : DynamicFixture {
  SparseFixture() {
    for (unsigned i = 1; i <= 3; ++i)
      put(ph(i, 48), 8, 0x4000);
    put(ph(3, 16), 8, 0x8000);
    put(ph(3, 40), 8, 0x6000);
    put(ph(5, 16), 8, 0x8000);
    put(ph(7, 16), 8, 0x8000);
    segment(4, PT_LOAD, PF_R | PF_W, 0, 0x10000, 0, 0x2100, page);
    set(DT_INIT_ARRAY, 0x8900);
    set(DT_FINI_ARRAY, 0x8910);
    put(0xb00, 8, 0x8800);
    put(0xb18, 8, 0x8808);
    // Bytes after each file extent are not BSS contents, even within the file.
    memset(bytes + 0x1000, 0x71, 0x1000);
    memset(bytes + 0x2100, 0x72, 0x1f00);
    memset(bytes + 0x5000, 0x73, 0x1000);
  }
};

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                  \
      return 1;                                                                \
    }                                                                          \
  } while (false)

static bool zero_bytes(Bytes bytes) {
  if (bytes.empty())
    return false;
  for (auto value : bytes)
    if (value)
      return false;
  return true;
}

extern "C" int main() {
  CHECK(is_user_range(reinterpret_cast<Address>(pool), sizeof(pool)));
  SparseFixture f;
  start(f);
  DsoMapping owner(page);
  CHECK(owner.open("libx.so").error == 0);
  unsigned baseline_calls = calls;
  CHECK(!fd_open && close_count == 1 && live_count() == 2);
  CHECK(reservations == 1 && fixed_maps == 5);
  CHECK(owner.metadata() && owner.metadata()->symbol_count() == 2);
  const auto &image = *owner.image();
  CHECK(!(image.load_bias() & 0x3fff));
  CHECK(zero_bytes(image.mapped_memory_bytes(0x1000, 0x1000, PF_R)));
  CHECK(zero_bytes(image.mapped_memory_bytes(0x2100, 0x1f00, PF_R)));
  CHECK(zero_bytes(image.mapped_memory_bytes(0x9000, 0x5000, PF_R)));
  CHECK(zero_bytes(image.mapped_memory_bytes(0x10000, 0x2100, PF_R)));
  CHECK(image.mapped_memory_bytes(0x4000, 1, PF_R).empty());
  Address origin = image.load_bias() - reinterpret_cast<Address>(pool);
  CHECK(protection[origin / page] == PROT_READ);
  CHECK(protection[(origin + 0x2000) / page] == (PROT_READ | PROT_EXEC));
  CHECK(protection[(origin + 0x4000) / page] == PROT_NONE);
  CHECK(protection[(origin + 0x6000) / page] == PROT_NONE);
  CHECK(protection[(origin + 0x8000) / page] == (PROT_READ | PROT_WRITE));
  CHECK(protection[(origin + 0xe000) / page] == PROT_NONE);
  CHECK(owner.open("again").error == -EBUSY);
  CHECK(owner.reset() == 0 && !live_count() && close_count == 1);
  CHECK(!owner.image() && !owner.metadata() && owner.reset() == 0);

  // Fail every syscall in a successful acquisition/commit path, including
  // mprotect, close and staging release. Failed unmaps remain explicitly owned.
  for (unsigned fail = 1; fail <= baseline_calls; ++fail) {
    start(f);
    fail_at = fail;
    MapResult result = owner.open("libx.so");
    CHECK(injected == 1 && result.error == -EIO);
    CHECK(!owner.image() && !owner.metadata() && !fd_open);
    CHECK(close_count == (fail == 1 ? 0u : 1u));
    if (live_count())
      CHECK(owner.open("busy").error == -EBUSY && result.cleanup_error == -EIO);
    CHECK(owner.reset() == 0 && !live_count());
    CHECK(close_count == (fail == 1 ? 0u : 1u));
  }

  start(f);
  fail_at = 8; // After both staging and address reservation acquisitions.
  fail_unmap = true;
  MapResult failed = owner.open("libx.so");
  CHECK(failed.error == -EIO && failed.cleanup_error == -EIO && live_count());
  CHECK(owner.reset() == -EIO && owner.open("busy").error == -EBUSY);
  fail_unmap = false;
  CHECK(owner.reset() == 0 && !live_count() && close_count == 1);

  start(f);
  actual_size = sizeof(f.bytes) - 1;
  CHECK(owner.open("short").error == -ENOEXEC && reservations == 0);
  CHECK(!live_count() && close_count == 1);
  start(f);
  actual_size = advertised_size = 0x4fff;
  CHECK(owner.open("truncated-segment").error == -ENOEXEC && reservations == 0);
  CHECK(!live_count() && close_count == 1);
  start(f);
  advertised_size = 63;
  CHECK(owner.open("header").error == -ENOEXEC && !live_count());
  start(f);
  advertised_size = __LONG_MAX__;
  CHECK(owner.open("overflow").error == -EOVERFLOW && !live_count());
  start(f);
  interrupt_read = true;
  read_limit = 7;
  CHECK(owner.open("partial").error == 0 && !fd_open);
  CHECK(owner.reset() == 0 && !live_count());

  DynamicFixture compact;
  compact.put(ph(1, 40), 8, 0x1000);
  compact.put(ph(2, 40), 8, 0x100);
  start(compact);
  CHECK(owner.open("without-readonly-bss").error == 0);
  CHECK(owner.metadata()->symbol_count() == 2);
  CHECK(owner.reset() == 0 && !live_count());

  // File and anonymous loads need not begin at a page boundary. File offsets
  // and linked addresses must retain congruence, including over-page alignment.
  SparseFixture unaligned;
  unaligned.put(ph(2, 8), 8, 0x2080);
  unaligned.put(ph(2, 16), 8, 0x2080);
  unaligned.put(ph(2, 32), 8, 0x80);
  unaligned.put(ph(2, 40), 8, 0x1f80);
  unaligned.put(0x820, 8, 0x2080);
  unaligned.set(DT_INIT, 0x2080);
  unaligned.set(DT_FINI, 0x2084);
  unaligned.put(ph(4, 8), 8, 0x100);
  unaligned.put(ph(4, 16), 8, 0x10100);
  start(unaligned);
  CHECK(owner.open("unaligned").error == 0);
  CHECK(zero_bytes(owner.image()->mapped_memory_bytes(0x2100, 0x1f00, PF_R)));
  CHECK(zero_bytes(owner.image()->mapped_memory_bytes(0x10100, 0x2100, PF_R)));
  CHECK(owner.reset() == 0 && !live_count());

  for (unsigned defect = 0; defect != 5; ++defect) {
    SparseFixture bad;
    if (defect == 0)
      bad.bytes[0] = 0;
    if (defect == 1)
      bad.set(DT_STRTAB, ~Address(0));
    if (defect == 2)
      bad.put(ph(1, 48), 8, 3);
    if (defect == 3)
      bad.put(ph(3, 40), 8, ~Address(0));
    if (defect == 4)
      bad.put(ph(3, 4), 4, PF_R | PF_W | PF_X);
    start(bad);
    CHECK(owner.open("bad").error == -ENOEXEC);
    CHECK(!live_count() && reservations == 0 && close_count == 1);
  }
  start(f);
  corrupt_mapping = true;
  CHECK(owner.open("changed").error == -ENOEXEC && fixed_maps);
  CHECK(!owner.image() && !owner.metadata() && !live_count());

  start(f);
  DsoMapping second(page), third(page);
  CHECK(owner.open("one").error == 0);
  CHECK(second.open("two").error == 0);
  const auto *first_image = owner.image();
  fail_at = calls + 8;
  CHECK(third.open("three").error == -EIO);
  CHECK(live_count() == 4 && owner.image() == first_image);
  CHECK(owner.metadata()->symbol_count() == 2 &&
        second.metadata()->symbol_count() == 2);
  CHECK(second.reset() == 0 && owner.metadata()->symbol_count() == 2);
  CHECK(owner.reset() == 0 && !live_count());
  start(f);
  DsoMapping wrong_page(4096);
  CHECK(wrong_page.open("wrong").error == -EINVAL && calls == 0);
  puts("loader DSO mapping ownership and injected failures passed");
  return 0;
}
