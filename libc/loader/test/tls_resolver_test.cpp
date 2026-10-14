//===-- Startup TLS resolver tests ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/io.h"
#include "loader/linux/tls_resolver.h"
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace loader_test;
namespace cpp = LIBC_NAMESPACE::cpp;
namespace mmix = LIBC_NAMESPACE::mmix;
static thread_local Address host_tp;
static void check(bool c, int line = __builtin_LINE()) {
  if (!c) {
    fprintf(stderr, "resolver check failed at line %d\n", line);
    abort();
  }
}
namespace LIBC_NAMESPACE_DECL {
namespace loader {
Address current_thread_pointer() { return host_tp; }
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  long result = syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
} // namespace loader
namespace mmix {
[[noreturn]] void fatal_dynamic_tls() { loader::fatal_startup(); }
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

struct alignas(8192) AlignedFixture : DynamicFixture {
  explicit AlignedFixture(ImageRole role) : DynamicFixture(role) {}
};
static AlignedFixture fixtures[2] = {AlignedFixture(ImageRole::MainPIE),
                                     AlignedFixture(ImageRole::SharedObject)};

int main() {
  static_assert(sizeof(TLSIndex) == 16);
  ElfImage images[2];
  DynamicInfo metadata[2];
  MappedRegion regions[2];
  WritableRegion writes[2];
  cpp::span<const WritableRegion> writable[2];
  SymbolObject objects[2];
  for (unsigned i = 0; i < 2; ++i) {
    auto &f = fixtures[i];
    f.segment(7, PT_TLS, PF_R, 0x4a00, 0x4a00, 8, 32, 64);
    regions[i] = {0, {f.bytes, sizeof(f.bytes)}};
    check(
        ElfImage::open_mapped({&regions[i], 1}, 0, 64,
                              i ? ImageRole::SharedObject : ImageRole::MainPIE,
                              reinterpret_cast<Address>(f.bytes), page,
                              images[i]) == ElfError::None);
    check(DynamicInfo::open(images[i], metadata[i]) == DynamicError::None);
    objects[i] = {&images[i], &metadata[i]};
    writes[i] = {0x4000, {f.bytes + 0x4000, 0x2000}};
    writable[i] = {&writes[i], 1};
  }
  StartupTLSModule modules[2];
  StartupTLSSet set;
  const ElfImage *views[] = {&images[0], &images[1]};
  check(plan_startup_tls(views, modules, set) == TLSLayoutError::None);
  RelocatedTLS ready;
  check(relocate_startup_tls(objects, writable, set, ready).error ==
        StartupRelocationError::None);
  TLSAllocationContext context{&ready, page};
  ThreadTLSAllocation allocations[2]{};
  for (auto &a : allocations)
    check(prepare_thread_tls(&context, nullptr, &a) == 0);
  TLSIndex index{1, 8};
  auto fails = [&](auto operation) {
    pid_t pid = fork();
    check(pid >= 0);
    if (!pid) {
      operation();
      _exit(1);
    }
    int status;
    check(waitpid(pid, &status, 0) == pid);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 127);
  };
  // Missing adoption is fatal, and failed adoption cannot poison later setup.
  fails([&] { __tls_get_addr(&index); });
  check(!mmix::adopt_startup_tls_resolver({nullptr, resolve_startup_tls}));
  check(!mmix::adopt_startup_tls_resolver({&context, nullptr}));
  check(mmix::adopt_startup_tls_resolver({&context, resolve_startup_tls}));
  check(!mmix::adopt_startup_tls_resolver({&context, resolve_startup_tls}));
  auto worker = [&](unsigned i) {
    host_tp = allocations[i].tp;
    for (unsigned iteration = 0; iteration < 32; ++iteration) {
      for (unsigned m = 0; m < 2; ++m) {
        TLSIndex gd{m + 1, 8}, ld{m + 1, 0};
        Address ie = host_tp + modules[m].tp_offset + 8;
        check(reinterpret_cast<Address>(__tls_get_addr(&gd)) == ie);
        check(reinterpret_cast<Address>(__llvm_libc_tls_get_addr(&ld)) + 8 ==
              ie);
        *reinterpret_cast<Address *>(ie) = i + 1;
        check(host_tp == allocations[i].tp);
      }
    }
  };
  std::thread first(worker, 0), second(worker, 1);
  first.join();
  second.join();
  for (unsigned i = 0; i < 2; ++i)
    for (const auto &m : modules)
      check(*reinterpret_cast<Address *>(allocations[i].tp + m.tp_offset + 8) ==
            i + 1);
  host_tp = allocations[0].tp;
  // Each child mutates its own mappings; no successful null fallback is
  // allowed.
  for (unsigned mutation = 0; mutation < 17; ++mutation) {
    fails([&] {
      auto *dtv =
          reinterpret_cast<StartupDTV *>(allocations[0].dtv_mapping_base);
      auto *entries = reinterpret_cast<StartupDTVEntry *>(dtv->entries);
      switch (mutation) {
      case 0:
        index.module_id = 0;
        break;
      case 1:
        index.module_id = 3;
        break;
      case 2:
        index.offset = 33;
        break;
      case 3:
        index.offset = ~Address(0);
        break;
      case 4:
        host_tp = 0;
        break;
      case 5:
        ++host_tp;
        break;
      case 6:
        dtv->generation = 2;
        break;
      case 7:
        dtv->capacity = 1;
        break;
      case 8:
        entries[1].generation = 0;
        break;
      case 9:
        entries[1].image_base = allocations[1].tp + modules[0].tp_offset;
        break;
      case 10:
        entries[1].allocation_owner = 1;
        break;
      case 11:
        entries[1].allocation_extent = 8;
        break;
      case 12:
        dtv->entries = 0;
        break;
      case 13:
        *reinterpret_cast<Address *>(host_tp) = 0;
        break;
      case 14:
        context.relocated = nullptr;
        break;
      case 15:
        __tls_get_addr(nullptr);
        break;
      case 16:
        modules[0].module_id = 2;
        break;
      }
      __tls_get_addr(&index);
    });
  }
  // Address formation permits the one-past value, not dereferencing it.
  index.offset = modules[0].memory_size;
  check(reinterpret_cast<Address>(__tls_get_addr(&index)) ==
        host_tp + modules[0].tp_offset + modules[0].memory_size);
  host_tp = 0;
  for (auto &a : allocations)
    check(release_thread_tls(&context, &a) == 0);
}
