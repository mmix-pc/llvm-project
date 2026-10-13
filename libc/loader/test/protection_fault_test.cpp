//===-- Native protection fault probes ------------------------------------===//
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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace loader_test;
static void check(bool ok, int line = __builtin_LINE()) {
  if (!ok) {
    fprintf(stderr, "native protection probe failed at line %d\n", line);
    abort();
  }
}
namespace LIBC_NAMESPACE_DECL {
namespace loader {
long raw_syscall(long number, long a, long b, long c, long d, long e, long f) {
  check(number == SYS_mprotect);
  long result = syscall(number, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL

static const void *fault_address;
static void denied(int, siginfo_t *info, void *) {
  _exit(info->si_code == SEGV_ACCERR && info->si_addr == fault_address ? 77
                                                                       : 101);
}

// Each denied access runs in its own native child. Verify both the fault class
// and address: an invalid instruction or unrelated crash is not a permission
// result. None of these native probes qualifies MMIX guest execution.
static void fault(unsigned char *address, bool write, bool execute = false) {
  pid_t child = fork();
  check(child >= 0);
  if (!child) {
    fault_address = address;
    struct sigaction action = {};
    action.sa_sigaction = denied;
    action.sa_flags = SA_SIGINFO;
    check(sigemptyset(&action.sa_mask) == 0);
    check(sigaction(SIGSEGV, &action, nullptr) == 0);
    if (execute)
      reinterpret_cast<void (*)()>(address)();
    else if (write)
      *reinterpret_cast<volatile unsigned char *>(address) = 1;
    else
      (void)*reinterpret_cast<volatile unsigned char *>(address);
    _exit(100);
  }
  int status;
  check(waitpid(child, &status, 0) == child);
  check(WIFEXITED(status) && WEXITSTATUS(status) == 77);
}

int main() {
  struct rlimit limit = {0, 0};
  check(setrlimit(RLIMIT_CORE, &limit) == 0);
  constexpr size_t size = 6 * page;
  auto *bytes = static_cast<unsigned char *>(
      mmap(reinterpret_cast<void *>(0x20000000), size, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  check(bytes != MAP_FAILED && !(reinterpret_cast<Address>(bytes) % page));
  DynamicFixture file;
  file.put(ph(2, 16), 8, 2 * page);
  file.put(ph(3, 16), 8, 4 * page);
  file.put(ph(3, 40), 8, 2 * page);
  file.put(ph(5, 16), 8, 4 * page);
  file.put(ph(7, 16), 8, 4 * page);
  memcpy(bytes, file.bytes, page);
  memcpy(bytes + 2 * page, file.bytes + page, page);
  memcpy(bytes + 4 * page, file.bytes + 2 * page, page);
  MappedRegion region{0, {bytes, size}};
  ElfImage image;
  check(ElfImage::open_mapped({&region, 1}, 0, 64, ImageRole::SharedObject,
                              reinterpret_cast<Address>(bytes), page,
                              image) == ElfError::None);
  check(mprotect(bytes + page, page, PROT_NONE) == 0);
  check(mprotect(bytes + 3 * page, page, PROT_NONE) == 0);
  const ElfImage *images[] = {&image};
  check(finalize_image_protections(images, page) == 0);
  for (size_t i = 5 * page; i < size; ++i)
    check(bytes[i] == 0);
  bytes[5 * page] = 42;
  check(bytes[5 * page] == 42 && bytes[0] == 0x7f);
  fault(bytes, true);                   // Read-only metadata.
  fault(bytes + 2 * page, true);        // RX text.
  fault(bytes + 4 * page, true);        // RELRO.
  fault(bytes + page, false);           // First hole.
  fault(bytes + 3 * page, false);       // Second hole.
  fault(bytes + 5 * page, false, true); // NX writable data.
  check(munmap(bytes, size) == 0);
  puts("native mmap/mprotect: BSS, RW, RELRO, RX write denial, holes and NX "
       "passed");
}
