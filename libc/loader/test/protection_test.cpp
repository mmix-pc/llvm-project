//===-- Final image protection tests --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/protection.h"
#include "loader/linux/syscall.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>

using namespace loader_test;
static void check(bool ok, int line = __builtin_LINE()) {
  if (!ok) {
    fprintf(stderr, "protection check failed at line %d\n", line);
    abort();
  }
}
struct Call {
  Address address, size;
  long prot;
};
static Call calls[32];
static unsigned count, fail_at;

namespace LIBC_NAMESPACE_DECL {
namespace loader {
long raw_syscall(long number, long a, long b, long c, long, long, long) {
  check(number == SYS_mprotect && count < 32);
  check(!(a % page) && b > 0 && !(b % page));
  check((c & (PROT_WRITE | PROT_EXEC)) != (PROT_WRITE | PROT_EXEC));
  calls[count++] = {Address(a), Address(b), c};
  return count == fail_at ? -EACCES : 0;
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL

extern "C" int main() {
  DynamicFixture main(ImageRole::MainPIE), dso;
  ElfImage images[2];
  MappedRegion regions[] = {{0, main.view()}, {0, dso.view()}};
  const ElfImage *scope[] = {&images[0], &images[1]};
  check(ElfImage::open_mapped({regions, 1}, 0, 64, ImageRole::MainPIE, bias,
                              page, images[0]) == ElfError::None);
  check(ElfImage::open_mapped({regions + 1, 1}, 0, 64, ImageRole::SharedObject,
                              bias + 0x100000, page,
                              images[1]) == ElfError::None);
  check(finalize_image_protections(scope, page) == 0 && count == 8);
  const int flags[] = {PROT_READ, PROT_READ | PROT_EXEC,
                       PROT_READ | PROT_WRITE};
  for (unsigned i = 0; i < 6; ++i) {
    check(calls[i].address == bias + (i / 3) * 0x100000 + (i % 3) * page);
    check(calls[i].size == page && calls[i].prot == flags[i % 3]);
  }
  for (unsigned i = 6; i < 8; ++i)
    check(calls[i].address == bias + (i - 6) * 0x100000 + 2 * page &&
          calls[i].size == page && calls[i].prot == PROT_READ);
  for (unsigned fail = 1; fail <= 8; ++fail) {
    count = 0;
    fail_at = fail;
    check(finalize_image_protections(scope, page) == -EACCES && count == fail);
  }
  fail_at = count = 0;
  check(finalize_image_protections(scope, 4096) == -EINVAL && !count);
  check(finalize_image_protections({}, page) == -EINVAL && !count);
  // Deliberate test-only corruption of the later object's borrowed headers.
  // A complete layout preflight must fail before protecting the first image.
  for (unsigned test = 0; test < 5; ++test) {
    dso = DynamicFixture();
    if (test == 0)
      dso.put(ph(3, 4), 4, PF_R | PF_W | PF_X);
    if (test == 1)
      dso.put(ph(7, 16), 8, 0x4001);
    if (test == 2)
      dso.put(ph(7, 40), 8, 2 * page);
    if (test == 3)
      dso.put(ph(6, 4), 4, PF_R | PF_W | PF_X);
    if (test == 4)
      dso.put(ph(3, 40), 8, ~Address(0));
    check(finalize_image_protections(scope, page) == -ENOEXEC && !count);
  }
  // Remove the middle LOAD to expose a page-sized hole. No mprotect may cover
  // it.
  dso = DynamicFixture();
  dso.put(ph(2), 4, PT_NULL);
  dso.put(ph(7, 40), 8, 0);
  count = 0;
  check(finalize_image_protections({scope + 1, 1}, page) == 0 && count == 2);
  check(calls[0].address == bias + 0x100000);
  check(calls[1].address == bias + 0x104000);
  puts("final image permissions, RELRO, holes and failure preflight passed");
  return 0;
}
