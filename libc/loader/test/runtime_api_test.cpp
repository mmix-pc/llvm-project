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
#include "loader/linux/runtime_unwind.h"
#include "src/dlfcn/dladdr.h"
#include "src/dlfcn/dlclose.h"
#include "src/dlfcn/dlerror.h"
#include "src/dlfcn/dlinfo.h"
#include "src/dlfcn/dlopen.h"
#include "src/dlfcn/dlsym.h"
#include "src/dlfcn/linux/mmix/loading.h"
#include "src/link/dl_iterate_phdr.h"
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
static bool fail_query_allocation;
static bool deliver_pending;
struct PendingCancellation {};
static uintptr_t caller_pc = bias + 0x2000, observed_pc;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    std::fprintf(stderr, "runtime API check at %d\n", line);
    std::abort();
  }
}
namespace LIBC_NAMESPACE::loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  if (n == SYS_mmap && fail_query_allocation) {
    fail_query_allocation = false;
    return -ENOMEM;
  }
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
  if (!saved && deliver_pending) {
    deliver_pending = false;
    throw PendingCancellation{};
  }
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
static void *peek(void *, const StartupTLSModule *module) {
  check(disabled && module->module_id == tls_module);
  return tls_calls ? &tls_storage : nullptr;
}
struct Enumeration {
  unsigned count = 0;
  const char *path;
  void *handle;
  bool reopen = false;
};
static int enumerate(dl_phdr_info *info, size_t size, void *opaque) {
  auto &state = *static_cast<Enumeration *>(opaque);
  check(size == sizeof(*info) && info->dlpi_phnum && info->dlpi_phdr);
  check(info->dlpi_adds == 2 && info->dlpi_subs == 0);
  ++state.count;
  // Reentry must not deadlock and must not change the enumerated image set.
  link_map *map = nullptr;
  check(!LIBC_NAMESPACE::dlinfo(state.handle, RTLD_DI_LINKMAP, &map) && map);
  if (state.reopen) {
    pthread_t writer;
    check(!pthread_create(
        &writer, nullptr,
        [](void *p) -> void * {
          auto &state = *static_cast<Enumeration *>(p);
          void *handle = client::loader_open(state.path, RTLD_NOW | RTLD_NOLOAD,
                                             caller_pc);
          check(handle && !LIBC_NAMESPACE::dlclose(handle));
          return nullptr;
        },
        &state));
    check(!pthread_join(
        writer, nullptr)); // Would deadlock if callback held the loader lock.
  }
  return 0;
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
  main_file.put(main_file.at(DT_CHECKSUM), 8, DT_DEBUG);
  main_file.set(DT_DEBUG, 0);
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
                     {&registry, resolve}, nullptr, peek);
  check(!service.query_metadata().bind_debug(main, 0x123000));
  RuntimeQueries failing_queries(page);
  fail_query_allocation = true;
  check(failing_queries.prepare(*registry.snapshot()) == -ENOMEM);
  check(!failing_queries.snapshot() && !failing_queries.debug_record()->r_map);
  check(!failing_queries.abort());
  check(!failing_queries.prepare(*registry.snapshot()));
  failing_queries.publish();
  check(failing_queries.snapshot()->records.size() == 1);
  DynamicInfo main_dynamic;
  check(DynamicInfo::open(main, main_dynamic) == DynamicError::None);
  check(main_dynamic.value(DynamicTag::Debug) ==
        reinterpret_cast<Address>(service.query_metadata().debug_record()));
  check(client::adopt_query_services(service.query_services()));
  check(!client::adopt_query_services(service.query_services()));
  check(!client::adopt_loader_process_services({}));
  check(client::adopt_loader_process_services(service.process_services()));
  check(!client::adopt_loader_process_services(service.process_services()));
  uint64_t saved_state;
  check(!enter(&saved_state));
  check(!client::prepare_loader_fork());
  client::finish_loader_fork(false);
  check(!leave(saved_state));
  production = service.services();
  check(
      !client::adopt_loading_services({production.context, nullptr, nullptr, nullptr}));
  check(!client::adopt_loading_services(
      {production.context, record_open, record_lookup, nullptr}));
  check(client::adopt_loading_services(
      {production.context, record_open, record_lookup, production.close}));
  check(!client::adopt_loading_services(production));

  // Restoration can unwind only after the service has released its locks.
  deliver_pending = true;
  bool delivered = false;
  try {
    client::loader_lookup(RTLD_DEFAULT, "absent", caller_pc);
  } catch (const PendingCancellation &) {
    delivered = true;
  }
  check(delivered && !disabled && !deliver_pending);
  check(!enter(&saved_state));
  check(!client::prepare_loader_fork());
  client::finish_loader_fork(false);
  check(!leave(saved_state));

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
  Enumeration first{0, argv[1], nullptr, false};
  check(!LIBC_NAMESPACE::dl_iterate_phdr(
      [](dl_phdr_info *info, size_t, void *p) {
        auto &state = *static_cast<Enumeration *>(p);
        check(info->dlpi_adds == 1 && !state.count++);
        state.handle = client::loader_open(
            state.path, RTLD_LAZY | RTLD_NODELETE, caller_pc);
        return 0;
      },
      &first));
  void *local = first.handle;
  check(first.count ==
        1); // The newly published image is not in this enumeration.
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
  static_assert(RTLD_DI_LINKMAP == InfoLinkMap &&
                RTLD_DI_TLS_MODID == InfoTLSModid &&
                RTLD_DI_TLS_DATA == InfoTLSData);
  Dl_info address_info{};
  check(LIBC_NAMESPACE::dladdr(function, &address_info) == 1);
  check(!std::strcmp(address_info.dli_sname, "access_tls") &&
        address_info.dli_saddr == function && address_info.dli_fname[0]);
  check(LIBC_NAMESPACE::dladdr(reinterpret_cast<const char *>(function) + 1,
                               &address_info) == 1 &&
        address_info.dli_saddr == function);
  check(LIBC_NAMESPACE::dladdr(reinterpret_cast<void *>(bias + 0x4000),
                               &address_info) == 1 &&
        address_info.dli_fbase == reinterpret_cast<void *>(bias));
  check(!LIBC_NAMESPACE::dladdr(reinterpret_cast<void *>(1), &address_info));
  check(LIBC_NAMESPACE::dlerror());
  link_map *map = nullptr;
  size_t module = 0;
  void *unused_tls = reinterpret_cast<void *>(1);
  check(!LIBC_NAMESPACE::dlinfo(local, RTLD_DI_LINKMAP, &map) && map->l_prev &&
        map->l_ld);
  check(!LIBC_NAMESPACE::dlinfo(local, RTLD_DI_TLS_MODID, &module) &&
        module == tls_module);
  check(!LIBC_NAMESPACE::dlinfo(local, RTLD_DI_TLS_DATA, &unused_tls) &&
        !unused_tls && !tls_calls);
  check(LIBC_NAMESPACE::dlinfo(local, 12345, &module) == -1 &&
        LIBC_NAMESPACE::dlerror());
  check(LIBC_NAMESPACE::dlinfo(RTLD_DEFAULT, RTLD_DI_LINKMAP, &map) == -1 &&
        LIBC_NAMESPACE::dlerror());
  Enumeration enumeration{0, argv[1], local, true};
  check(!LIBC_NAMESPACE::dl_iterate_phdr(enumerate, &enumeration) &&
        enumeration.count == 2);
  unsigned stops = 0;
  check(LIBC_NAMESPACE::dl_iterate_phdr(
            [](dl_phdr_info *, size_t, void *p) {
              ++*static_cast<unsigned *>(p);
              return 27;
            },
            &stops) == 27 &&
        stops == 1);
  check(!service.query_metadata().active_readers());
  try {
    LIBC_NAMESPACE::dl_iterate_phdr(
        [](dl_phdr_info *, size_t, void *) -> int { throw 73; }, nullptr);
    check(false);
  } catch (int value) {
    check(value == 73);
  }
  check(!service.query_metadata().active_readers() && !disabled);
  pthread_t exiting;
  check(!pthread_create(
      &exiting, nullptr,
      [](void *) -> void * {
        LIBC_NAMESPACE::dl_iterate_phdr(
            [](dl_phdr_info *, size_t, void *) -> int {
              pthread_exit(reinterpret_cast<void *>(7));
            },
            nullptr);
        return nullptr;
      },
      nullptr));
  void *exit_value;
  check(!pthread_join(exiting, &exit_value) &&
        exit_value == reinterpret_cast<void *>(7));
  check(!service.query_metadata().active_readers());
  auto *debug = service.query_metadata().debug_record();
  check(debug->r_version == 1 && debug->r_state == 0 && debug->r_brk &&
        debug->r_ldbase == 0x123000);
  check(debug->r_map && debug->r_map->l_next && !debug->r_map->l_next->l_next);
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
  auto retained_lease = scopes.acquire();
  const auto *retained = retained_lease.get();
  const auto *retained_images = registry.snapshot();
  UnwindImageView unwind{};
  check(!find_runtime_unwind_image(&scopes,
          reinterpret_cast<uintptr_t>(function), &unwind));
  void *tls_address = client::loader_lookup(local, "tls_zero", caller_pc);
  check(tls_address == &tls_storage);
  check(!LIBC_NAMESPACE::dlinfo(local, RTLD_DI_TLS_DATA, &unused_tls) &&
        unused_tls == tls_address);
  tls_storage = 73;
  void *invalid_handles[] = {RTLD_DEFAULT, RTLD_NEXT, reinterpret_cast<void *>(999)};
  for (void *bad : invalid_handles) {
    check(LIBC_NAMESPACE::dlclose(bad) == -1);
    check(LIBC_NAMESPACE::dlerror() && scopes.snapshot() == retained);
  }
  check(!LIBC_NAMESPACE::dlclose(local) && !LIBC_NAMESPACE::dlerror());
  check(LIBC_NAMESPACE::dlclose(local) == -1 && LIBC_NAMESPACE::dlerror());
  check(!client::loader_lookup(local, "access_tls", caller_pc));
  check(client::loader_error());
  check(LIBC_NAMESPACE::dlinfo(local, RTLD_DI_LINKMAP, &map) == -1 &&
        LIBC_NAMESPACE::dlerror());
  check(client::loader_lookup(promoted, "access_tls", caller_pc) == function);
  check(!LIBC_NAMESPACE::dlclose(promoted));
  check(!LIBC_NAMESPACE::dlclose(global));
  check(scopes.snapshot()->handles.empty());
  check(registry.snapshot() == retained_images &&
        scopes.snapshot()->unwind->registry == retained->unwind->registry);
  RuntimeScopes::Value value;
  check(RuntimeScopes::lookup(*retained, reinterpret_cast<uintptr_t>(local),
                              text("access_tls"), value) == SymbolError::None);
  check(client::loader_lookup(RTLD_DEFAULT, "access_tls", caller_pc) == function);
  for (unsigned i = 0; i < 3; ++i) {
    void *reopened = client::loader_open(argv[1], RTLD_NOW | RTLD_NOLOAD, caller_pc);
    check(reopened && reopened != local && reopened != promoted);
    check(client::loader_lookup(reopened, "tls_zero", caller_pc) == tls_address);
    check(tls_storage == 73 && registry.snapshot()->images[1] == image);
    check(!LIBC_NAMESPACE::dlclose(reopened) && !LIBC_NAMESPACE::dlerror());
  }
  UnwindImageView after{};
  check(!find_runtime_unwind_image(&scopes,
          reinterpret_cast<uintptr_t>(function), &after));
  check(after.token && after.eh_frame_header == unwind.eh_frame_header);
  release_runtime_unwind_image(&scopes, unwind.token);
  release_runtime_unwind_image(&scopes, after.token);
  check(scopes.snapshot()->handles.empty() && !disabled);
  check(debug->r_map->l_next && debug->r_state == 0);
  // PHDR/debug views retire independently of raw-pointer query pins. An old
  // callback lease retains its list while new readers see the deletion.
  RuntimeQueries retiring_queries(page);
  check(!retiring_queries.prepare(*retained_images));
  retiring_queries.publish();
  {
    auto reader = retiring_queries.acquire();
    ImageRegistry::Snapshot reduced{retained_images->generation,
                                    {retained_images->images.data(), 1}};
    check(!retiring_queries.prepare(reduced));
    retiring_queries.publish();
    check(retiring_queries.snapshot()->records.size() == 1);
    check(retiring_queries.snapshot()->adds == retained_images->images.size() &&
          retiring_queries.snapshot()->subs == retained_images->images.size() - 1);
    check(retiring_queries.debug_record()->r_state == 0 &&
          !retiring_queries.debug_record()->r_map->l_next);
    check(RuntimeQueries::record(*reader.get(), image));
    check(!retiring_queries.readers_drained(image));
    check(retiring_queries.collect_snapshots() == -EBUSY);
  }
  check(retiring_queries.readers_drained(image));
  check(!retiring_queries.collect_snapshots());
  std::puts("image query, reentrant enumeration, nonallocating TLS and "
            "debugger record checks passed");
  std::puts("logical close, stale handles, balanced reopen and resident TLS/unwind checks passed");
  std::puts("runtime API linked opens, flags, caller scopes, zero symbols, "
            "TLS bridge and per-thread error checks passed");
}
