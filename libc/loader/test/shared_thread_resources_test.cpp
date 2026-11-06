//===-- Composed shared thread resource rollback tests
//---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Reuse the actual loader fixture and its syscall/TP fault transport. The
// original standalone test retains its own executable and assertions.
#define main existing_thread_tls_main
#include "loader/test/thread_tls_test.cpp"
#undef main
// Include the production bridge so the test can install its private service
// table without pretending to execute target startup or changing a public ABI.
#include "src/__support/threads/linux/mmix/resources.h"
#include "startup/linux/mmix/shared_adopt.cpp"
using namespace LIBC_NAMESPACE::mmix;
static void *resource_maps[2], *resource_storage[2];
static size_t resource_sizes[2];
static unsigned resource_count, resource_failures, resource_unmaps;
extern "C" long __llvm_libc_mmix_syscall(unsigned long n, long a, long b, long,
                                         long, long, long) {
  if (n == SYS_mmap) {
    check(resource_count < 2);
    // Native pages need not have the target's 8192-byte alignment. Keep the
    // enclosing host mapping separately from the resource-visible region.
    void *storage = ::mmap(nullptr, b + 8192, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(storage != MAP_FAILED);
    void *p = reinterpret_cast<void *>(
        (reinterpret_cast<uintptr_t>(storage) + 8191) & ~uintptr_t(8191));
    resource_storage[resource_count] = storage;
    resource_maps[resource_count] = p;
    resource_sizes[resource_count++] = b;
    return reinterpret_cast<long>(p);
  }
  if (n == SYS_mprotect || n == SYS_futex)
    return 0;
  check(n == SYS_munmap);
  for (unsigned i = 0; i < 2; ++i) {
    if (resource_maps[i] != reinterpret_cast<void *>(a))
      continue;
    check(resource_sizes[i] == size_t(b));
    ++resource_unmaps;
    if (resource_failures & (1u << i))
      return -EIO;
    check(!::munmap(resource_storage[i], resource_sizes[i] + 8192));
    resource_storage[i] = nullptr;
    resource_maps[i] = nullptr;
    return 0;
  }
  check(false);
  return -EINVAL;
}
struct Owner {
  unsigned refs = 1, retains = 0, attempts = 0, releases = 0;
  unsigned resident = 0;
  long error = 0;
};
static long retain(void *p) {
  auto &o = *static_cast<Owner *>(p);
  check(o.refs == 1 && !o.retains++);
  ++o.refs;
  return 0;
}
static long release(void *p) {
  auto &o = *static_cast<Owner *>(p);
  check(!resource_maps[0] && !resource_maps[1]);
  check(live_count == o.resident && o.refs == 2 && !o.releases);
  ++o.attempts;
  if (o.error)
    return o.error;
  ++o.releases;
  --o.refs;
  return 0;
}
static void *runner(void *p) { return p; }
static void reset_resources() {
  check(!resource_maps[0] && !resource_maps[1]);
  resource_count = resource_failures = resource_unmaps = 0;
}
extern "C" int main() {
  FixtureState f;
  f.publish();
  // Inject the loader service boundary, retaining actual bridge and owner code.
  LIBC_NAMESPACE::mmix::services.context = &f.context;
  LIBC_NAMESPACE::mmix::services.prepare_thread_tls =
      prepare_runtime_thread_tls;
  LIBC_NAMESPACE::mmix::services.release_thread_tls =
      release_runtime_thread_tls;
  const unsigned resident = live_count;
  for (unsigned loader_failure = 0; loader_failure <= 4; ++loader_failure) {
    for (unsigned resources_failure = 0; resources_failure < 4;
         ++resources_failure) {
      for (unsigned owner_failure = 0; owner_failure < 2; ++owner_failure) {
        reset_resources();
        Owner owner;
        owner.resident = resident;
        ThreadPreparation input;
        input.runner.posix_runner = runner;
        input.resource_owner = {&owner, retain, release};
        ThreadControl *control = nullptr;
        check(prepare_thread(input, control).error == ThreadPrepareError::None);
        check(control && owner.refs == 2 && owner.retains == 1);
        auto resources = thread_resources(*control);
        f.lookup(resources.shared_tls, 3);
        tp = 0;
        fail_unmap = loader_failure ? unmaps + loader_failure : 0;
        resource_failures = resources_failure;
        owner.error = owner_failure ? ENOMEM : 0;
        long error = release_thread_resources(resources);
        check(error ==
              ((loader_failure || resources_failure) ? EIO : owner.error));
        check(!resources.tls.tp && !resources.tls.addr && !resources.tls.size);
        check(bool(resources.control.size) == bool(resources_failure & 1));
        check(bool(resources.stack.size) == bool(resources_failure & 2));
        if (loader_failure || resources_failure)
          check(!owner.attempts && owner.refs == 2);
        if (loader_failure)
          check(resources.shared_tls.runtime_owner);
        fail_unmap = resource_failures = 0;
        if (error) {
          check(release_thread_resources(resources) == owner.error);
          if (owner.error) {
            check(resources.owner.context == &owner && owner.refs == 2);
            unsigned tls_unmaps = unmaps, plain_unmaps = resource_unmaps;
            owner.error = 0;
            check(!release_thread_resources(resources));
            check(unmaps == tls_unmaps && resource_unmaps == plain_unmaps);
          }
        }
        check(owner.refs == 1 && owner.releases == 1 &&
              !resources.owns_mappings());
        unsigned tls_unmaps = unmaps, plain_unmaps = resource_unmaps,
                 attempts = owner.attempts;
        check(!release_thread_resources(resources));
        check(unmaps == tls_unmaps && resource_unmaps == plain_unmaps &&
              owner.attempts == attempts);
        check(live_count == resident);
      }
    }
  }
  for (unsigned allocation_failure = 1; allocation_failure <= 4;
       ++allocation_failure) {
    reset_resources();
    Owner owner;
    ThreadPreparation input;
    input.runner.posix_runner = runner;
    input.resource_owner = {&owner, retain, release};
    ThreadControl *control = reinterpret_cast<ThreadControl *>(uintptr_t(1));
    fail_map = maps + allocation_failure;
    fail_all_unmaps = true;
    resource_failures = 3;
    auto result = prepare_thread(input, control);
    check(result.error == ThreadPrepareError::TLSFailed &&
          result.syscall_error == ENOMEM);
    check(control == reinterpret_cast<ThreadControl *>(uintptr_t(1)));
    check(!owner.retains && !owner.attempts && owner.refs == 1);
    check(result.retained.control.size && result.retained.stack.size);
    check(bool(result.retained.shared_tls.runtime_owner) ==
          (allocation_failure != 1));
    fail_map = resource_failures = 0;
    fail_all_unmaps = false;
    check(!release_thread_resources(result.retained));
    check(!result.retained.owns_mappings() && live_count == resident);
  }
  std::puts("shared resources, bridge and loader: 40 release and 4 preparation "
            "failure combinations passed");
  return 0;
}
