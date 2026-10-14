//===-- Resident image registry tests -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "elf_fixture.h"
#include "loader/linux/image_registry.h"
#include "loader/linux/syscall.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace loader_test;
static bool fail_map, fail_unmap;
static int maps_before_failure = -1;
static unsigned mappings, unmappings;

static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    fprintf(stderr, "image registry failure at %d\n", line);
    abort();
  }
}

namespace LIBC_NAMESPACE::loader {
long raw_syscall(long number, long a1, long a2, long a3, long a4, long a5,
                 long a6) {
  if (number == SYS_mmap) {
    if (fail_map || maps_before_failure == 0)
      return -ENOMEM;
    if (maps_before_failure > 0)
      --maps_before_failure;
    long result = ::syscall(number, a1, a2, a3, a4, a5, a6);
    if (result == -1)
      return -errno;
    ++mappings;
    return result;
  }
  check(number == SYS_munmap);
  if (fail_unmap)
    return -EIO;
  if (::munmap(reinterpret_cast<void *>(a1), a2))
    return -errno;
  ++unmappings;
  return 0;
}
} // namespace LIBC_NAMESPACE::loader

struct Input {
  Fixture file;
  ElfImage image;
  explicit Input(ImageRole role = ImageRole::SharedObject) : file(role) {
    file.segment(7, PT_TLS, PF_R, 0x4808, 0x4808, 8, 24, 64);
    check(ElfImage::open_file(file.view(), role, bias, page, image) ==
          ElfError::None);
  }
};

static void ready(ImageRegistry::Transaction &transaction) {
  check(!transaction.mark_tls_ready());
  check(!transaction.mark_protected());
  check(transaction.mark_relocated());
  check(!transaction.mark_relocated());
  check(transaction.mark_tls_ready());
  check(transaction.mark_protected());
  check(!transaction.mark_protected());
}

extern "C" int main() {
  const size_t host_page = ::sysconf(_SC_PAGESIZE);
  Input main(ImageRole::MainPIE), initial, late, other, last;
  const ElfImage *startup[] = {&main.image, &initial.image};
  const ElfImage *batch[] = {&late.image};
  ImageRegistry registry(host_page);
  ImageRegistry::Transaction first(host_page);
  check(!registry.snapshot() && !first.view());
  check(registry.publish(first) == -EINVAL);
  const ElfImage *invalid[] = {nullptr};
  check(registry.prepare(first, invalid) == -EINVAL);
  check(registry.prepare(first, {}) == -EINVAL);
  check(registry.prepare(first, batch) == -EINVAL);
  check(registry.prepare(first, startup) == 0);
  check(!registry.snapshot() && first.view()->generation == 1);
  check(first.view()->images[0]->tls.module_id == 1);
  check(first.abort() == 0);
  check(registry.prepare(first, startup) == 0);
  check(first.view()->images[0]->tls.module_id == 1);
  check(first.view()->images[0]->tls.tp_offset == 72);
  check(first.view()->images[1]->tls.tp_offset == 136);
  check(registry.publish(first) == -EINVAL);
  ready(first);
  ImageRegistry foreign(host_page);
  check(foreign.publish(first) == -EINVAL);
  check(registry.publish(first) == 0);
  const auto *old = registry.snapshot();
  check(first.abort() == -EBUSY && registry.publish(first) == -EINVAL);
  check(registry.prepare(first, batch) == -EBUSY);
  check(old->find(1, 1) == old->images[0]);
  check(!old->find(1, 2) && !old->find(0, 0));

  ImageRegistry::Transaction a(host_page), b(host_page);
  check(registry.prepare(a, startup) == -EINVAL);
  const ElfImage *duplicate[] = {&initial.image};
  check(registry.prepare(a, duplicate) == -EEXIST);
  const ElfImage *repeated[] = {&late.image, &late.image};
  check(registry.prepare(a, repeated) == -EEXIST);
  check(registry.prepare(a, batch) == 0);
  uint64_t discarded_id = a.view()->images[2]->tls.module_id;
  uint64_t discarded_generation = a.view()->generation;
  check(a.view()->images[0] == old->images[0]);
  check(a.view()->images[2]->tls.tp_offset == 0);
  check(!a.view()->images[2]->startup);
  check(a.view()->images[2]->tls.alignment == 64);
  check(a.view()->images[2]->tls.residue == 8);
  check(registry.snapshot() == old && old->images.size() == 2);
  const ElfImage *second[] = {&other.image};
  check(registry.prepare(b, second) == 0);
  ready(b);
  check(registry.publish(b) == 0);
  ready(a);
  check(registry.publish(a) == -EAGAIN);
  check(!registry.snapshot()->find(discarded_id, discarded_generation));
  check(a.abort() == 0);
  check(registry.prepare(a, batch) == 0);
  check(a.view()->images[3]->tls.module_id > discarded_id);
  check(a.view()->generation > b.view()->generation);
  ready(a);
  check(registry.publish(a) == 0);
  check(old->images.size() == 2 && old->images[0]->tls.tp_offset == 72);
  check(b.view()->images.size() == 3 &&
        registry.snapshot()->images.size() == 4);

  ImageRegistry::Transaction failed(host_page);
  batch[0] = &last.image;
  fail_map = true;
  const auto *before = registry.snapshot();
  check(registry.prepare(failed, batch) == -ENOMEM);
  check(!failed.view() && registry.snapshot() == before);
  check(registry.prepare(failed, batch) == -EBUSY);
  fail_map = false;
  check(failed.abort() == 0);
  check(registry.prepare(failed, batch) == 0);
  fail_unmap = true;
  check(failed.abort() == -EIO && !failed.view());
  check(registry.prepare(failed, batch) == -EBUSY);
  check(registry.snapshot() == before);
  fail_unmap = false;
  check(failed.abort() == 0);

  Input many[20];
  const ElfImage *large[20];
  for (unsigned i = 0; i < 20; ++i)
    large[i] = &many[i].image;
  maps_before_failure = 1;
  unsigned allocated = mappings, released_partial = unmappings;
  check(registry.prepare(failed, large) == -ENOMEM);
  check(mappings == allocated + 1 && !failed.view());
  check(registry.snapshot() == before);
  maps_before_failure = -1;
  check(failed.abort() == 0 && unmappings == released_partial + 1);

  ImageRegistry::Transaction mismatch(host_page * 2);
  check(registry.prepare(mismatch, batch) == 0);
  ready(mismatch);
  check(registry.publish(mismatch) == -EINVAL);
  check(registry.snapshot() == before && mismatch.abort() == 0);

  // Transfer preserves views and removes only the source's ownership.
  Arena source(host_page), destination(host_page), wrong(host_page * 2);
  void *p;
  check(source.allocate(16, 8, p) == 0);
  static_cast<char *>(p)[0] = 42;
  check(!wrong.adopt(source) && !source.adopt(source));
  unsigned released = unmappings;
  check(destination.adopt(source));
  check(source.reset() == 0 && unmappings == released);
  check(static_cast<char *>(p)[0] == 42);
  check(destination.reset() == 0 && unmappings == released + 1);
  check(mappings > unmappings); // Published registry storage is resident.

  RegistrySequence ids;
  uint64_t id = 99;
  check(!ids.reserve(0, id) && id == 99);
  check(ids.reserve(~uint64_t(0) - 1, id) && id == 1);
  check(!ids.reserve(2, id) && id == 1);
  check(ids.reserve(1, id) && id == ~uint64_t(0));
  check(!ids.can_reserve(1) && !ids.reserve(1, id));
  return 0;
}
