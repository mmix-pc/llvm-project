//===-- Runtime loading API tests with linked MMIX inputs
//------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/runtime_api.h"
#include "src/dlfcn/dlerror.h"
#include "src/dlfcn/dlopen.h"
#include "src/dlfcn/dlsym.h"
#include "src/dlfcn/linux/mmix/loading.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace loader_test;
namespace client = LIBC_NAMESPACE::mmix;
static thread_local unsigned disabled;
static thread_local unsigned tls_calls;
static thread_local long tls_storage;
static LoaderAPI production;
static bool inject_nested_error;
static uintptr_t caller_pc = bias + 0x2000, observed_pc;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    std::fprintf(stderr, "runtime API check at %d\n", line);
    std::abort();
  }
}
namespace LIBC_NAMESPACE::loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  if (n == SYS_mmap && !(d & MAP_FIXED))
    d |= MAP_32BIT;
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
} // namespace LIBC_NAMESPACE::loader
static long enter(uint64_t *saved) {
  *saved = disabled++;
  return 0;
}
static long leave(uint64_t saved) {
  check(disabled == saved + 1);
  disabled = static_cast<unsigned>(saved);
  return 0;
}
namespace LIBC_NAMESPACE::mmix {
loader::LoaderCancellation loader_cancellation_hooks() {
  return {enter, leave};
}
} // namespace LIBC_NAMESPACE::mmix
static Bytes text(const char *s) {
  return {reinterpret_cast<const unsigned char *>(s), std::strlen(s)};
}
static uint64_t tls_module;
static uintptr_t tls_offset;
static void *resolve(void *, const TLSIndex *index) {
  check(disabled && index && index->module_id == tls_module &&
        index->offset == tls_offset);
  ++tls_calls;
  return &tls_storage; // Index bridge only; actual TLS resolver has its own
                       // test.
}
static long record_open(void *p, const char *name, int flags, uintptr_t pc,
                        LoaderCancellation hooks, uint64_t *out) {
  observed_pc = pc;
  long error = production.open(p, name, flags, pc, hooks, out);
  if (!error && inject_nested_error) {
    inject_nested_error = false;
    check(!client::loader_lookup(reinterpret_cast<void *>(999), "foo", pc));
  }
  return error;
}
static long record_lookup(void *p, uint64_t handle, const char *name,
                          uintptr_t pc, LoaderCancellation hooks,
                          uintptr_t *out) {
  // The public wrapper capture probe runs before the concurrent tests.
  return production.lookup(p, handle, name, pc, hooks, out);
}
struct Worker {
  void *handle;
  void *tls;
  char *message;
};
static void *worker(void *opaque) {
  auto &out = *static_cast<Worker *>(opaque);
  check(!client::loader_error());
  out.tls = client::loader_lookup(out.handle, "tls_zero", caller_pc);
  check(out.tls == &tls_storage && tls_calls == 1);
  check(!client::loader_error());
  check(!client::loader_lookup(out.handle, "absent", caller_pc));
  out.message = client::loader_error();
  check(out.message && std::strstr(out.message, "not found"));
  check(!client::loader_error() && !disabled);
  return nullptr;
}
int main(int argc, char **argv) {
  check(argc == 4);
  static_assert(RTLD_LAZY == OpenLazy && RTLD_NOW == OpenNow &&
                RTLD_GLOBAL == OpenGlobal && RTLD_NOLOAD == OpenNoLoad &&
                RTLD_NODELETE == OpenNoDelete && RTLD_LOCAL == 0);
  check(reinterpret_cast<uintptr_t>(RTLD_DEFAULT) == LookupDefault &&
        reinterpret_cast<uintptr_t>(RTLD_NEXT) == LookupNext);
  check(!client::loader_open(nullptr, RTLD_NOW, caller_pc));
  check(std::strstr(client::loader_error(), "unavailable"));
  check(!client::loader_error());

  DynamicFixture main_file(ImageRole::MainPIE);
  main_file.put(0x4900, 8, bias + 0x2008);
  main_file.put(0x4908, 8, bias + 0x200c);
  main_file.put(0x4910, 8, bias + 0x2010);
  // A valid absolute-zero symbol must not be confused with lookup failure.
  main_file.put(0x81e, 2, SHN_ABS);
  main_file.put(0x820, 8, 0);
  MappedRegion region{0, main_file.view()};
  ElfImage main;
  check(ElfImage::open_mapped({&region, 1}, 0, 64, ImageRole::MainPIE, bias,
                              page, main) == ElfError::None);
  ImageRegistry registry(page);
  ImageRegistry::Transaction initial(page);
  const ElfImage *inputs[] = {&main};
  ImageRegistry::Discovery discovery[] = {
      {false, {}, text("/test/main"), ~size_t(0), {}}};
  check(!registry.prepare(initial, inputs, discovery));
  check(initial.mark_relocated() && initial.mark_tls_ready() &&
        initial.mark_protected() && !registry.publish(initial));
  RuntimeScopes scopes(page);
  RuntimeScopes::Transaction startup_scopes(page);
  check(!scopes.prepare_startup(*registry.snapshot(), startup_scopes));
  check(!scopes.prepare_unwind(startup_scopes) &&
        !scopes.publish(startup_scopes));
  RuntimeInitializers initializers;
  RuntimeInitializers::Batch startup_init(page);
  check(!initializers.prepare(*registry.snapshot(), startup_init));
  check(!initializers.attach(startup_init, *registry.snapshot()));
  check(!initializers.adopt_startup(startup_init, registry.snapshot()->images));
  InitialStack stack;
  stack.valid = true;
  SearchEnvironment environment;
  check(!environment.parse(false, {}, {}));
  RuntimeAPI service(page, registry, scopes, initializers, stack, environment,
                     {&registry, resolve});
  production = service.services();
  check(
      !client::adopt_loading_services({production.context, nullptr, nullptr}));
  check(client::adopt_loading_services(
      {production.context, record_open, record_lookup}));
  check(!client::adopt_loading_services(production));

  const int invalid_flags[] = {0, RTLD_NOW | RTLD_LAZY,
                               RTLD_NOW | RTLD_DEEPBIND, RTLD_NOW | 0x8000, -1};
  for (int flags : invalid_flags) {
    check(!client::loader_open(nullptr, flags, caller_pc));
    check(std::strstr(client::loader_error(), "invalid"));
  }
  check(!client::loader_open(nullptr, RTLD_NOW, 1));
  check(client::loader_error());
  inject_nested_error = true;
  void *global = client::loader_open(nullptr, RTLD_LAZY, caller_pc);
  check(global && !client::loader_error());
  check(!client::loader_lookup(global, "foo", caller_pc));
  check(!client::loader_error()); // Successful zero.
  check(
      !client::loader_lookup(reinterpret_cast<void *>(999), "foo", caller_pc));
  char *saved = client::loader_error();
  check(saved && std::strstr(saved, "invalid") && !client::loader_error());
  check(std::strstr(saved, "invalid")); // Consumption does not destroy storage.
  check(!client::loader_lookup(global, nullptr, caller_pc));
  check(client::loader_error());

  // These calls exercise the real public wrappers, not the explicit-PC helper.
  // Host return PCs are deliberately outside the synthetic MMIX image ranges.
  check(!LIBC_NAMESPACE::dlopen(nullptr, RTLD_NOW));
  check(observed_pc && observed_pc != caller_pc);
  check(LIBC_NAMESPACE::dlerror());
  check(!LIBC_NAMESPACE::dlsym(global, "foo") && LIBC_NAMESPACE::dlerror());

  const auto *before = scopes.snapshot();
  check(!client::loader_open(argv[1], RTLD_NOW | RTLD_NOLOAD, caller_pc));
  check(client::loader_error() && scopes.snapshot() == before);
  for (int i = 2; i < 4; ++i) {
    check(!client::loader_open(argv[i], RTLD_NOW, caller_pc));
    check(client::loader_error() && scopes.snapshot() == before);
  }
  void *local =
      client::loader_open(argv[1], RTLD_LAZY | RTLD_NODELETE, caller_pc);
  check(local && !client::loader_error());
  void *function = client::loader_lookup(local, "access_tls", caller_pc);
  check(function && !client::loader_error());
  check(!client::loader_lookup(RTLD_DEFAULT, "access_tls", caller_pc));
  check(client::loader_error());
  const auto *image = registry.snapshot()->images[1];
  DynamicInfo dynamic;
  check(DynamicInfo::open(image->elf, dynamic) == DynamicError::None);
  Address index;
  Elf64_Sym symbol;
  check(dynamic.find_definition(text("tls_zero"), index) &&
        dynamic.symbol(index, symbol));
  tls_module = image->tls.module_id;
  tls_offset = symbol.st_value;
  void *promoted = client::loader_open(
      argv[1], RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL, caller_pc);
  check(promoted && promoted != local && !client::loader_error());
  check(client::loader_lookup(RTLD_DEFAULT, "access_tls", caller_pc) ==
        function);
  check(client::loader_lookup(RTLD_NEXT, "access_tls", caller_pc) == function);
  check(!client::loader_lookup(RTLD_NEXT, "access_tls",
                               reinterpret_cast<uintptr_t>(function)));
  check(client::loader_error());
  Worker results[2]{{local}, {promoted}};
  pthread_t threads[2];
  for (unsigned i = 0; i < 2; ++i)
    check(!pthread_create(threads + i, nullptr, worker, results + i));
  for (auto &thread : threads)
    check(!pthread_join(thread, nullptr));
  check(results[0].tls != results[1].tls &&
        results[0].message != results[1].message);
  check(!client::loader_error() && !tls_calls && !disabled);
  std::puts("runtime API linked opens, flags, caller scopes, zero symbols, "
            "TLS bridge and per-thread error checks passed");
}
