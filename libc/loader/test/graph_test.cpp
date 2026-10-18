//===-- Startup dependency graph tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/runtime_retirement.h"
#include "loader/linux/graph.h"
#include "loader/linux/runtime_graph.h"
#include "loader/linux/runtime_load.h"
#include "loader/linux/initialization.h"
#include "loader/linux/startup.h"
#include "loader/linux/symbols.h"
#include "loader/linux/syscall.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/stat.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace loader_test;
namespace LIBC_NAMESPACE::loader {
r_debug *runtime_debug_record() {
  static r_debug record{};
  return &record;
}
} // namespace LIBC_NAMESPACE::loader
template <size_t N> static Bytes text(const char (&s)[N]) {
  return Bytes(reinterpret_cast<const unsigned char *>(s), N - 1);
}

struct Library : DynamicFixture {
  unsigned string_size = 0x40;
  explicit Library(ImageRole role = ImageRole::SharedObject)
      : DynamicFixture(role) {
    memset(bytes + 0x4000, 0, 0x1000);
    entries = 0;
    add(DT_STRTAB, 0x700);
    add(DT_STRSZ, string_size);
    add(DT_SYMTAB, 0x800);
    add(DT_SYMENT, 24);
    add(DT_HASH, 0x900);
    add(DT_GNU_HASH, 0xa00);
    add(DT_SONAME, 13); // All distinct files deliberately share "self.so".
    add(DT_RPATH, 21);  // $ORIGIN
    finish();
  }
  void finish() {
    put(0x4000 + entries * 16, 8, DT_NULL);
    put(0x4008 + entries * 16, 8, 0);
    put(ph(5, 32), 8, (entries + 1) * 16);
    put(ph(5, 40), 8, (entries + 1) * 16);
    set(DT_STRSZ, string_size);
  }
  void need(const char *name) {
    add(DT_NEEDED, string_size);
    do {
      assert(string_size < 0x100);
      bytes[0x700 + string_size++] = *name;
    } while (*name++);
    finish();
  }
  void initializers(bool main = false) {
    add(DT_INIT, 0x2000);
    add(DT_INIT_ARRAY, 0x4900);
    add(DT_INIT_ARRAYSZ, 16);
    if (main) {
      add(DT_PREINIT_ARRAY, 0x4920);
      add(DT_PREINIT_ARRAYSZ, 8);
    }
    unsigned count = main ? 3 : 2;
    for (unsigned i = 0; i < count; ++i) {
      put(0xb00 + 24 * i, 8, i == 2 ? 0x4920 : 0x4900 + 8 * i);
      put(0xb08 + 24 * i, 8, 41);
      put(0xb10 + 24 * i, 8, 0x2004 + 4 * i);
    }
    add(DT_FINI, 0x2018);
    add(DT_FINI_ARRAY, 0x4940);
    add(DT_FINI_ARRAYSZ, 16);
    for (unsigned i = 0; i < 2; ++i) {
      put(0xb00 + 24 * (count + i), 8, 0x4940 + 8 * i);
      put(0xb08 + 24 * (count + i), 8, 41);
      put(0xb10 + 24 * (count + i), 8, 0x2010 + 4 * i);
    }
    count += 2;
    add(DT_RELA, 0xb00);
    add(DT_RELASZ, count * 24);
    add(DT_RELAENT, 24);
    add(DT_RELACOUNT, count);
    finish();
  }
};

struct File {
  const char *path;
  Library *contents;
  FileIdentity identity;
};
static File files[8];
static unsigned file_count, active_file, calls, fail_at, closes, opens,
    mapped_files;
static bool fd_live, fail_unmaps, no_identity, interrupt_stat;
static Address fail_release_address;
static size_t file_position;
alignas(page) static unsigned char pool[8 * 1024 * 1024];
struct Allocation {
  Address address, size;
  bool reservation, live;
};
static Allocation allocations[128];
static size_t used;
static unsigned allocations_count;
static long failed_syscall;
static Address borrowed_main;
static const char *executable_name = "/lib/main";

static unsigned live() {
  unsigned count = 0;
  for (unsigned i = 0; i < allocations_count; ++i)
    count += allocations[i].live;
  return count;
}
static void start() {
  assert(!fd_live && !live());
  calls = fail_at = closes = opens = mapped_files = 0;
  failed_syscall = 0;
  used = allocations_count = 0;
  fail_unmaps = no_identity = interrupt_stat = false;
}
static Allocation *owner(Address address, Address size) {
  for (unsigned i = 0; i < allocations_count; ++i) {
    auto &a = allocations[i];
    if (a.live && address >= a.address && size <= a.size &&
        address - a.address <= a.size - size)
      return &a;
  }
  return nullptr;
}

namespace LIBC_NAMESPACE_DECL {
namespace loader {
Address current_thread_pointer() { return 0; }
long raw_syscall(long number, long a, long b, long c, long d, long e, long f) {
  if (number == SYS_gettid)
    return 1;
  ++calls;
  if (number == SYS_close) {
    assert(fd_live && a == 7);
    fd_live = false;
    ++closes;
  }
  if (calls == fail_at) {
    failed_syscall = number;
    return -EIO;
  }
  if (number == SYS_openat) {
    assert(!fd_live);
    for (unsigned i = 0; i < file_count; ++i) {
      if (!strcmp(reinterpret_cast<const char *>(b), files[i].path)) {
        active_file = i;
        fd_live = true;
        file_position = 0;
        ++opens;
        return 7;
      }
    }
    return -ENOENT;
  }
  if (number == SYS_close)
    return 0;
  if (number == SYS_readlinkat) {
    assert(fd_live && a == AT_FDCWD);
    assert(!strcmp(reinterpret_cast<const char *>(b), "/proc/self/fd/7"));
    size_t length = strlen(executable_name);
    if (length > size_t(d))
      length = d;
    memcpy(reinterpret_cast<void *>(c), executable_name, length);
    return length;
  }
  if (number == SYS_statx) {
    assert(fd_live && a == 7);
    assert(!*reinterpret_cast<const char *>(b) && c == AT_EMPTY_PATH &&
           d == (STATX_INO | STATX_TYPE));
    if (interrupt_stat) {
      interrupt_stat = false;
      return -EINTR;
    }
    auto &out = *reinterpret_cast<struct statx *>(e);
    out.stx_mask = no_identity ? STATX_TYPE : STATX_INO | STATX_TYPE;
    out.stx_mode = S_IFREG;
    out.stx_ino = files[active_file].identity.inode;
    out.stx_dev_major = files[active_file].identity.device_major;
    out.stx_dev_minor = files[active_file].identity.device_minor;
    return 0;
  }
  if (number == SYS_lseek) {
    assert(fd_live && a == 7);
    file_position = c == SEEK_END ? sizeof(Library::bytes) : 0;
    return file_position;
  }
  if (number == SYS_read) {
    assert(fd_live && a == 7);
    size_t size = sizeof(Library::bytes) - file_position;
    if (size > size_t(c))
      size = c;
    memcpy(reinterpret_cast<void *>(b),
           files[active_file].contents->bytes + file_position, size);
    file_position += size;
    return size;
  }
  if (number == SYS_getcwd) {
    assert(b >= 5);
    memcpy(reinterpret_cast<void *>(a), "/lib", 5);
    return 5;
  }
  if (number == SYS_mmap) {
    assert(b > 0 && !(b & (page - 1)));
    assert((c & (PROT_EXEC | PROT_WRITE)) != (PROT_EXEC | PROT_WRITE));
    Address address;
    if (d & MAP_FIXED) {
      auto *range = owner(a, b);
      assert(range && range->reservation);
      address = a;
    } else {
      assert(used + b <= sizeof(pool) && allocations_count < 128);
      address = reinterpret_cast<Address>(pool + used);
      used += b;
      allocations[allocations_count++] = {address, Address(b), c == PROT_NONE,
                                          true};
    }
    if (d & MAP_ANONYMOUS) {
      memset(reinterpret_cast<void *>(address), 0, b);
    } else {
      assert(fd_live && e == 7 && f >= 0 && size_t(f) < sizeof(Library::bytes));
      if (!f)
        ++mapped_files;
      size_t size = sizeof(Library::bytes) - f;
      if (size > size_t(b))
        size = b;
      memcpy(reinterpret_cast<void *>(address),
             files[active_file].contents->bytes + f, size);
      memset(reinterpret_cast<void *>(address + size), 0, b - size);
    }
    return address;
  }
  if (number == SYS_mprotect) {
    assert(owner(a, b) ||
           (borrowed_main && Address(a) >= borrowed_main &&
            Address(a + b) <= borrowed_main + 3 * page) ||
           (Address(a) >= bias && Address(a + b) <= bias + 3 * page));
    assert((c & (PROT_EXEC | PROT_WRITE)) != (PROT_EXEC | PROT_WRITE));
    return 0;
  }
  if (number == SYS_munmap) {
    auto *range = owner(a, b);
    assert(range && range->address == Address(a) && range->size == Address(b));
    if (fail_unmaps || Address(a) == fail_release_address)
      return -EIO;
    range->live = false;
    memset(reinterpret_cast<void *>(a), 0xa5, b);
    return 0;
  }
  assert(false && "unexpected syscall");
  return -ENOSYS;
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                  \
      return 1;                                                                \
    }                                                                          \
  } while (false)

static Address trace[32];
static size_t trace_count;
static StartupGraph *active_graph;
static StartupInitializers *active_initializers;
static InitialStack arguments{};
static void record_fini(Address address) {
  assert(trace_count < 32);
  active_initializers->finalize(record_fini);
  trace[trace_count++] = address;
}
static size_t stop_after;
static void stop_initialization(Address address, int, char **, char **) {
  trace[trace_count++] = address;
  if (trace_count == stop_after)
    active_initializers->finalize(record_fini);
}
static void record_init(Address address, int argc, char **argv, char **envp) {
  assert(trace_count < 32 && argc == 2 &&
         reinterpret_cast<Address>(argv) == arguments.argv &&
         reinterpret_cast<Address>(envp) == arguments.envp);
  assert(active_initializers->run(arguments, record_init) == -EBUSY);
  assert(active_graph->reset() == -EBUSY);
  trace[trace_count++] = address;
}

static void record_startup(Address address, int argc, char **argv, char **envp) {
  assert(trace_count < 32 && argc == 2 &&
         reinterpret_cast<Address>(argv) == arguments.argv &&
         reinterpret_cast<Address>(envp) == arguments.envp);
  trace[trace_count++] = address;
}

static int startup_tests() {
  alignas(page) static Library executable(ImageRole::MainPIE);
  alignas(page) static Fixture interpreter(ImageRole::Interpreter);
  alignas(8) static unsigned char env[16];
  static unsigned char random[16];
  static const char setting[] = "LD_LIBRARY_PATH=/lib";
  borrowed_main = reinterpret_cast<Address>(executable.bytes);
  Address self_base = reinterpret_cast<Address>(interpreter.bytes);
  MappedRegion regions[] = {
      {0, Bytes(interpreter.bytes, page)},
      {page, Bytes(interpreter.bytes + page, page)},
      {2 * page, Bytes(interpreter.bytes + 2 * page, page)}};
  ElfImage self;
  CHECK(ElfImage::open_mapped(regions, 0, 64, ImageRole::Interpreter,
                            self_base, page, self) == ElfError::None);
  arguments.valid = true;
  arguments.big_endian = true;
  arguments.argc = 2;
  arguments.argv = 0x8000;
  arguments.envp = reinterpret_cast<Address>(env);
  arguments.auxv = arguments.envp + sizeof(env);
  Address pointer = reinterpret_cast<Address>(setting);
  for (unsigned i = 0; i < 8; ++i)
    env[7 - i] = pointer >> (8 * i);
  arguments.values[unsigned(AuxField::Phdr)] = borrowed_main + 64;
  arguments.values[unsigned(AuxField::Phent)] = 56;
  arguments.values[unsigned(AuxField::Phnum)] = 8;
  arguments.values[unsigned(AuxField::Pagesz)] = page;
  arguments.values[unsigned(AuxField::Base)] = self_base;
  arguments.values[unsigned(AuxField::Entry)] = borrowed_main + page;
  arguments.values[unsigned(AuxField::Random)] = reinterpret_cast<Address>(random);
  arguments.values[unsigned(AuxField::Secure)] = 0;
  executable.initializers(true);
  files[0] = {"/proc/self/exe", &executable, {1, 1, 0}};
  file_count = 1;
  start();
  Startup startup(page);
  CHECK(!startup.prepare(arguments, self).error);
  unsigned boundaries = calls;
  CHECK(startup.entry() == borrowed_main + page && !trace_count);
  pid_t child = fork();
  CHECK(child >= 0);
  if (!child) {
    auto *registry = startup.unwind_registry();
    CHECK(registry && registry->is_published());
    CHECK(startup.unwind_registry() == registry);
    CHECK(startup.reset() == -EBUSY);
    CHECK(!startup.run(record_startup) && trace_count == 4);
    CHECK(startup.unwind_registry() == registry);
    _exit(0);
  }
  int status;
  CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
        WEXITSTATUS(status) == 0);
  CHECK(!startup.run(record_startup) && trace_count == 4);
  CHECK(!startup.unwind_registry()); // Publication must precede callbacks.
  CHECK(trace[0] == borrowed_main + page + 12);
  CHECK(!startup.run(record_startup) && trace_count == 4);
  CHECK(startup.prepare(arguments, self).error == -EBUSY);
  CHECK(!startup.reset() && !live() && !fd_live && closes == opens);
  trace_count = 0;

  for (unsigned failure = 1; failure <= boundaries; ++failure) {
    start();
    fail_at = failure;
    Startup failed(page);
    auto result = failed.prepare(arguments, self);
    bool optional = failed_syscall == SYS_openat || failed_syscall == SYS_statx ||
                    failed_syscall == SYS_readlinkat;
    CHECK(result.error == (optional ? 0 : -EIO));
    if (optional) {
      CHECK(failed.entry() == borrowed_main + page);
      CHECK(!failed.run(record_startup) && trace_count == 4);
      trace_count = 0;
    } else {
      CHECK(!failed.entry() && failed.run(record_startup) == -EINVAL);
    }
    CHECK(!failed.reset() && !live() && !fd_live && opens == closes);
    CHECK(!trace_count && executable.bytes[0] == 0x7f);
  }
  start();
  executable_name = "/lib/main (deleted)";
  Startup deleted(page);
  CHECK(!deleted.prepare(arguments, self).error);
  CHECK(!deleted.reset() && !live() && !fd_live);
  executable_name = "/lib/main";
  start();
  files[0].path = "/unavailable/proc/self/exe";
  Startup no_proc(page);
  CHECK(!no_proc.prepare(arguments, self).error);
  CHECK(!no_proc.prepare_runtime() && !no_proc.thread_pointer());
  CHECK(no_proc.prepare_runtime() == -EINVAL);
  CHECK(no_proc.adopt_runtime() == -EINVAL);
  CHECK(!no_proc.run(record_startup) && trace_count == 4);
  CHECK(!no_proc.reset() && !live() && !fd_live && !opens);
  trace_count = 0;
  files[0].path = "/proc/self/exe";
  // Startup IE admission does not supply a runtime owner. Failure must leave
  // TP unpublished, callbacks unexecuted and the transaction reclaimable.
  executable.add(DT_FLAGS, DF_STATIC_TLS);
  executable.finish();
  start();
  Startup missing_runtime(page);
  CHECK(!missing_runtime.prepare(arguments, self).error);
  CHECK(missing_runtime.prepare_runtime() == -ENOEXEC);
  CHECK(!missing_runtime.thread_pointer() && !trace_count);
  CHECK(missing_runtime.adopt_runtime() == -EINVAL);
  CHECK(missing_runtime.prepare_runtime() == -EINVAL);
  CHECK(missing_runtime.prepare(arguments, self).error == -EBUSY);
  CHECK(!missing_runtime.reset() && !live() && !fd_live);
  executable.set(DT_FLAGS, 0);
  start();
  arguments.values[unsigned(AuxField::Secure)] = 1;
  Startup secure(page);
  CHECK(secure.prepare(arguments, self).error == -EINVAL && !calls);
  arguments.values[unsigned(AuxField::Secure)] = 0;
  // The same physical main image can be a fixed ET_EXEC: all linked virtual
  // addresses become absolute while AT_ENTRY/AT_PHDR remain runtime addresses.
  executable.put(16, 2, ET_EXEC);
  executable.put(24, 8, borrowed_main + page);
  for (unsigned i = 0; i < 8; ++i) {
    Elf64_Phdr p;
    CHECK(decode_program_header(executable.view().subspan(ph(i), 56), true, p));
    if (p.p_type != PT_GNU_STACK)
      executable.put(ph(i, 16), 8, p.p_vaddr + borrowed_main);
  }
  executable.set(DT_STRTAB, borrowed_main + 0x700);
  executable.set(DT_SYMTAB, borrowed_main + 0x800);
  executable.set(DT_HASH, borrowed_main + 0x900);
  executable.set(DT_GNU_HASH, borrowed_main + 0xa00);
  executable.set(DT_RELA, borrowed_main + 0xb00);
  executable.set(DT_INIT, borrowed_main + 0x2000);
  executable.set(DT_INIT_ARRAY, borrowed_main + 0x4900);
  executable.set(DT_PREINIT_ARRAY, borrowed_main + 0x4920);
  executable.set(DT_FINI, borrowed_main + 0x2018);
  executable.set(DT_FINI_ARRAY, borrowed_main + 0x4940);
  executable.put(0x820, 8, borrowed_main + 0x2000);
  for (unsigned i = 0; i < 3; ++i) {
    executable.put(0xb00 + 24 * i, 8,
                   borrowed_main + (i == 2 ? 0x4920 : 0x4900 + 8 * i));
    executable.put(0xb10 + 24 * i, 8, borrowed_main + 0x2004 + 4 * i);
  }
  for (unsigned i = 0; i < 2; ++i) {
    executable.put(0xb00 + 24 * (3 + i), 8, borrowed_main + 0x4940 + 8 * i);
    executable.put(0xb10 + 24 * (3 + i), 8, borrowed_main + 0x2010 + 4 * i);
  }
  start();
  Startup fixed(page);
  CHECK(!fixed.prepare(arguments, self).error);
  CHECK(fixed.entry() == borrowed_main + page);
  CHECK(!fixed.run(record_startup) && trace_count == 4);
  CHECK(!fixed.reset() && !live() && !fd_live);
  trace_count = 0;
  borrowed_main = 0;
  return 0;
}

static int runtime_tests() {
  start();
  Library main_file(ImageRole::MainPIE), old, leaf, b, c, d, alternative;
  old.need("gone.so");
  old.add(DT_RUNPATH, 21);
  old.finish();
  leaf.need("old.so");
  b.need("oldalias.so");
  b.need("C.so");
  b.need("D.so");
  b.need("Calias.so");
  c.need("B.so");
  Library *initial[] = {&main_file, &old, &leaf};
  MappedRegion regions[3][3];
  ElfImage images[3];
  const ElfImage *views[3];
  for (unsigned i = 0; i < 3; ++i) {
    for (unsigned j = 0; j < 3; ++j)
      regions[i][j] = {j * page, Bytes(initial[i]->bytes + j * page, page)};
    CHECK(ElfImage::open_mapped(
              regions[i], 0, 64,
              i ? ImageRole::SharedObject : ImageRole::MainPIE,
              bias + i * 0x10000, page, images[i]) == ElfError::None);
    views[i] = &images[i];
  }
  size_t main_edges[] = {1}, old_edges[] = {2}, leaf_edges[] = {1};
  ImageRegistry::Discovery origins[] = {
      {true, {1, 1, 0}, text("/origin/main"), ~size_t(0), main_edges},
      {true, {2, 1, 0}, text("/origin/old.so"), 0, old_edges},
      {true, {3, 1, 0}, text("/origin/gone.so"), 1, leaf_edges}};
  ImageRegistry registry(page);
  ImageRegistry::Transaction transaction(page);
  CHECK(!registry.prepare(transaction, views, origins));
  CHECK(transaction.mark_relocated() && transaction.mark_tls_ready() &&
        transaction.mark_protected() && !registry.publish(transaction));
  const auto *snapshot = registry.snapshot();
  CHECK(snapshot->images[1]->dependencies[0] == snapshot->images[2]);
  CHECK(snapshot->images[2]->dependencies[0] == snapshot->images[1]);
  CHECK(snapshot->images[1]->ancestry.size() == 1);
  CHECK(snapshot->images[0]->path.data() != origins[0].path.data());
  const unsigned resident_live = live(),
                 resident_allocations = allocations_count;
  const size_t resident_used = used;
  auto rewind = [&]() {
    assert(live() == resident_live && !fd_live);
    used = resident_used;
    allocations_count = resident_allocations;
    calls = fail_at = closes = opens = mapped_files = 0;
    fail_unmaps = false;
  };
  files[0] = {"/origin/B.so", &b, {4, 1, 0}};
  files[1] = {"/origin/oldalias.so", &old, {2, 1, 0}};
  files[2] = {"/origin/C.so", &c, {5, 1, 0}};
  files[3] = {"/origin/Calias.so", &c, {5, 1, 0}};
  files[4] = {"/origin/D.so", &d, {5, 2, 0}};
  files[5] = {"/env/B.so", &alternative, {6, 1, 0}};
  file_count = 6;
  SearchEnvironment environment;
  Bytes env[] = {text("LD_LIBRARY_PATH=/env")};
  CHECK(!environment.parse(false, env, text("/origin")));
  RuntimeGraph graph(page);
  rewind();
  const auto &original = *snapshot->images[0];
  ImageRegistry::Image foreign{original.elf, original.tls, original.startup};
  CHECK(graph.build(*snapshot, foreign, text("B.so"), environment).error ==
        -EINVAL);
  CHECK(!calls && live() == resident_live);
  CHECK(!graph.build(*snapshot, *snapshot->images[0], text("B.so"), environment)
             .error);
  const unsigned baseline = calls;
  CHECK(graph.size() == 5 && graph.new_images() == 3 && mapped_files == 3);
  const RuntimeGraph::Node *nodes[5];
  unsigned index = 0;
  for (auto *node = graph.scope(); node; node = node->next()) {
    CHECK(index < 5);
    nodes[index++] = node;
  }
  CHECK(index == 5 && nodes[1]->existing() == snapshot->images[1] &&
        nodes[4]->existing() == snapshot->images[2]);
  CHECK(nodes[0]->dependencies()->target == nodes[1]);
  CHECK(nodes[0]->dependencies()->next->target == nodes[2]);
  CHECK(nodes[0]->dependencies()->next->next->target == nodes[3]);
  CHECK(nodes[0]->dependencies()->next->next->next->target == nodes[2]);
  CHECK(nodes[2]->dependencies()->target == nodes[0]);
  CHECK(nodes[1]->dependencies()->target == nodes[4]);
  CHECK(nodes[4]->dependencies()->target == nodes[1]);
  CHECK(registry.snapshot() == snapshot && snapshot->images.size() == 3);
  CHECK(!graph.reset() && live() == resident_live && opens == closes);

  // Reopen by file identity preserves old edges even when their files vanished.
  rewind();
  CHECK(!graph
             .build(*snapshot, *snapshot->images[0], text("oldalias.so"),
                    environment, true)
             .error);
  CHECK(graph.size() == 2 && !graph.new_images() && !mapped_files &&
        opens == 1);
  CHECK(!graph.reset() && opens == closes && live() == resident_live);
  rewind();
  CHECK(graph
            .build(*snapshot, *snapshot->images[0], text("B.so"), environment,
                   true)
            .error == -ENOENT);
  CHECK(!mapped_files && opens == 1 && opens == closes &&
        live() == resident_live);

  // A caller's RUNPATH is below LD_LIBRARY_PATH; main's RPATH was above it.
  rewind();
  CHECK(!graph.build(*snapshot, *snapshot->images[1], text("B.so"), environment)
             .error);
  CHECK(graph.size() == 1 && graph.scope()->identity() == files[5].identity);
  CHECK(!graph.reset() && live() == resident_live);

  // A new root with no useful RPATH inherits the published caller's ancestry.
  rewind();
  files[0].path = "/other/B.so";
  files[6] = {"/origin/B.so", &b, {4, 1, 0}};
  file_count = 7;
  b.set(DT_RPATH, 0);
  CHECK(!graph
             .build(*snapshot, *snapshot->images[0], text("/other/B.so"),
                    environment)
             .error);
  CHECK(graph.size() == 5 && graph.new_images() == 3);
  CHECK(!graph.reset() && live() == resident_live);
  b.set(DT_RPATH, 21);
  files[0].path = "/origin/B.so";
  file_count = 6;

  // Every syscall acquisition/release failure leaves the existing registry
  // unchanged. The mock consumes close even on error and rejects double close.
  for (unsigned fail = 1; fail <= baseline; ++fail) {
    rewind();
    fail_at = fail;
    auto result =
        graph.build(*snapshot, *snapshot->images[0], text("B.so"), environment);
    CHECK((result.error || result.cleanup_error) && !graph.scope());
    CHECK(registry.snapshot() == snapshot && snapshot->images.size() == 3);
    CHECK(!graph.reset() && live() == resident_live && !fd_live &&
          opens == closes);
    CHECK(main_file.bytes[0] == 0x7f && old.bytes[0] == 0x7f);
  }
  rewind();
  files[2].path = "/missing/C.so";
  auto missing =
      graph.build(*snapshot, *snapshot->images[0], text("B.so"), environment);
  CHECK(missing.error == -ENOENT && !missing.cleanup_error &&
        live() == resident_live);
  CHECK(opens == closes && !graph.scope());
  files[2].path = "/origin/C.so";

  rewind();
  fail_at = 8;
  fail_unmaps = true;
  auto failed =
      graph.build(*snapshot, *snapshot->images[0], text("B.so"), environment);
  CHECK(failed.error && failed.cleanup_error && live() > resident_live);
  CHECK(!graph.scope() && graph.reset() == -EIO);
  CHECK(graph.build(*snapshot, *snapshot->images[0], text("B.so"), environment)
            .error == -EBUSY);
  fail_unmaps = false;
  CHECK(!graph.reset() && live() == resident_live && opens == closes);

  // An unknown caller ORIGIN never falls back to cwd. Discovery metadata is
  // copied into the registry transaction, including its dependency arrays.
  rewind();
  ImageRegistry anonymous(page);
  ImageRegistry::Transaction anon_tx(page);
  const ElfImage *anon_images[] = {&images[0]};
  ImageRegistry::Discovery unknown[] = {{}};
  CHECK(!anonymous.prepare(anon_tx, anon_images, unknown));
  unsigned before = opens;
  CHECK(graph
            .build(*anon_tx.view(), *anon_tx.view()->images[0],
                   text("$ORIGIN/B.so"), environment)
            .error == -ENOENT);
  CHECK(opens == before && !graph.scope());
  CHECK(!anon_tx.abort() && live() == resident_live);

  // Bad parent/edge indices and duplicate identities fail before publication.
  rewind();
  origins[0].parent = 0;
  CHECK(anonymous.prepare(anon_tx, views, origins) == -EINVAL);
  origins[0].parent = ~size_t(0);
  main_edges[0] = 3;
  CHECK(anonymous.prepare(anon_tx, views, origins) == -EINVAL);
  main_edges[0] = 1;
  origins[1].identity = origins[0].identity;
  CHECK(anonymous.prepare(anon_tx, views, origins) == -EEXIST);
  origins[1].identity = {2, 1, 0};
  CHECK(live() == resident_live);

  rewind();
  CHECK(!graph.retain());
  CHECK(!graph.build(*snapshot, *snapshot->images[0], text("B.so"), environment)
             .error);
  auto *root = graph.scope();
  auto *new_c = root->next()->next();
  auto *new_d = new_c->next();
  const ElfImage *late[] = {root->image(), new_c->image(), new_d->image()};
  size_t b_edges[] = {1, 4, 5, 4}, c_edges[] = {3};
  ImageRegistry::Discovery late_info[] = {
      {true, root->identity(), root->path(), 0, b_edges},
      {true, new_c->identity(), new_c->path(), 3, c_edges},
      {true, new_d->identity(), new_d->path(), 3, {}}};
  ImageRegistry::Transaction late_tx(page);
  CHECK(!registry.prepare(late_tx, late, late_info));
  CHECK(late_tx.mark_relocated() && late_tx.mark_tls_ready() &&
        late_tx.mark_protected() && !registry.publish(late_tx));
  CHECK(graph.retain() && !graph.retain() && graph.reset() == -EBUSY);
  CHECK(snapshot->images.size() == 3 &&
        registry.snapshot()->images.size() == 6);
  RuntimeGraph reopen(page);
  unsigned old_maps = mapped_files;
  CHECK(!reopen
             .build(*registry.snapshot(), *snapshot->images[0], text("B.so"),
                    environment, true)
             .error);
  CHECK(reopen.size() == 5 && !reopen.new_images() && mapped_files == old_maps);
  CHECK(reopen.scope()->existing() == registry.snapshot()->images[3]);
  CHECK(!reopen.reset() && opens == closes);
  registry.snapshot()->images[3]->ownership.begin_finalization();
  for (unsigned no_load = 0; no_load != 2; ++no_load) {
    CHECK(reopen.build(*registry.snapshot(), *snapshot->images[0], text("B.so"),
                       environment, no_load).error == -EBUSY);
    CHECK(!reopen.reset() && opens == closes && mapped_files == old_maps);
  }
  // C's retained dependency on B cannot bypass the same admission decision.
  CHECK(reopen.build(*registry.snapshot(), *snapshot->images[0], text("C.so"),
                     environment, true).error == -EBUSY);
  CHECK(!reopen.reset() && opens == closes);
  // This child exits with intentionally resident registry and graph mappings.
  return 0;
}

static int runtime_load_tests() {
  start();
  Library main_file(ImageRole::MainPIE), a, b;
  a.need("B.so");
  b.need("A.so");
  a.initializers();
  b.initializers();
  // A real relocated pointer in the retained TLS template.
  a.segment(4, PT_TLS, PF_R, 0x4900, 0x4900, 8, 16, 32);
  MappedRegion regions[] = {{0, Bytes(main_file.bytes, page)},
                           {page, Bytes(main_file.bytes + page, page)},
                           {2 * page, Bytes(main_file.bytes + 2 * page, page)}};
  ElfImage image;
  CHECK(ElfImage::open_mapped(regions, 0, 64, ImageRole::MainPIE, bias, page,
                            image) == ElfError::None);
  const ElfImage *views[] = {&image};
  ImageRegistry::Discovery discovery[] = {
      {true, {1, 1, 0}, text("/lib/main"), ~size_t(0), {}}};
  ImageRegistry registry(page);
  ImageRegistry::Transaction startup(page);
  RuntimeScopes scopes(page);
  RuntimeScopes::Transaction initial_scopes(page);
  CHECK(!registry.prepare(startup, views, discovery));
  CHECK(startup.mark_relocated() && startup.mark_tls_ready() &&
        startup.mark_protected() && !registry.publish(startup));
  CHECK(!scopes.prepare_startup(*registry.snapshot(), initial_scopes));
  CHECK(!scopes.publish(initial_scopes));
  const auto *before = scopes.snapshot();
  const auto *caller = before->registry->images[0];
  const unsigned resident_live = live(), resident_allocations = allocations_count;
  const size_t resident_used = used;
  auto rewind = [&]() {
    assert(live() == resident_live && !fd_live);
    used = resident_used;
    allocations_count = resident_allocations;
    calls = fail_at = closes = opens = mapped_files = 0;
    fail_unmaps = false;
  };
  files[0] = {"/lib/A.so", &a, {2, 1, 0}};
  files[1] = {"/lib/B.so", &b, {3, 1, 0}};
  file_count = 2;
  SearchEnvironment environment;
  CHECK(!environment.parse(false, {}, text("/lib")));
  // Exercise the public service on an isolated mapping model. The caller stays
  // in the permanent main image; no stale code pointer is ever executed.
  pid_t api_child = fork();
  CHECK(api_child >= 0);
  if (!api_child) {
    RuntimeInitializers initializers;
    RuntimeInitializers::Batch boot(page);
    CHECK(!initializers.prepare(*registry.snapshot(), boot, true));
    CHECK(!initializers.attach(boot, *registry.snapshot()));
    CHECK(!initializers.adopt_startup(boot, registry.snapshot()->images));
    InitialStack arguments{};
    arguments.valid = true;
    static unsigned starts, finishes;
    RuntimeAPI runtime(page, registry, scopes, initializers, arguments,
                       environment, {},
                       [](Address, int, char **, char **) { ++starts; },
                       nullptr, nullptr, [](Address) { ++finishes; });
    auto api = runtime.services();
    LoaderCancellation hooks{[](uint64_t *saved) -> long {
                               *saved = 0;
                               return 0;
                             }, [](uint64_t) -> long { return 0; }};
    uint64_t first, second;
    CHECK(!api.open(api.context, "/lib/A.so", OpenNow, bias + page, hooks, &first));
    auto *old = registry.snapshot()->images[1];
    Address old_bias = old->elf.load_bias();
    uint64_t old_id = old->tls.module_id;
    CHECK(starts == 6 && !finishes);
    CHECK(!api.open(api.context, "/lib/A.so", OpenNow | OpenNoLoad,
                    bias + page, hooks, &second));
    CHECK(!api.close(api.context, first, hooks) && !finishes);
    CHECK(owner(old_bias, 1));
    CHECK(api.close(api.context, first, hooks) == EINVAL);
    CHECK(!api.close(api.context, second, hooks) && finishes == 6);
    CHECK(!owner(old_bias, 1) && registry.snapshot()->images.size() == 1);
    CHECK(api.open(api.context, "/lib/A.so", OpenNow | OpenNoLoad,
                   bias + page, hooks, &first) == ENOENT);
    CHECK(!api.open(api.context, "/lib/A.so", OpenNow, bias + page, hooks, &first));
    auto *fresh = registry.snapshot()->images[1];
    CHECK(fresh != old && fresh->tls.module_id > old_id && starts == 12);
    // B depends on A, so a B handle retains both members of the cycle.
    CHECK(!api.open(api.context, "/lib/B.so", OpenNow | OpenNoLoad,
                    bias + page, hooks, &second));
    CHECK(!api.close(api.context, first, hooks) && finishes == 6);
    CHECK(owner(fresh->elf.load_bias(), 1));
    auto image_api = runtime.image_services();
    uint64_t lease = 0;
    CHECK(!image_api.retain(image_api.context, fresh->elf.load_bias(),
                            fresh->elf.load_bias() + page, hooks, &lease));
    CHECK(!api.close(api.context, second, hooks) && finishes == 6);
    CHECK(owner(fresh->elf.load_bias(), 1));
    CHECK(!image_api.release(image_api.context, lease, hooks));
    // Release never runs callbacks inline. An explicit close supplies the next
    // safe point; autonomous release dispatch has its own integration boundary.
    CHECK(!api.open(api.context, nullptr, OpenNow, bias + page, hooks, &second));
    CHECK(!api.close(api.context, second, hooks) && finishes == 12);
    CHECK(!api.open(api.context, "/lib/A.so", OpenNow | OpenNoDelete,
                    bias + page, hooks, &first));
    auto *pinned = registry.snapshot()->images[1];
    CHECK(!api.close(api.context, first, hooks) && finishes == 12);
    CHECK(pinned->ownership.is_permanent() && owner(pinned->elf.load_bias(), 1));
    CHECK(!api.open(api.context, "/lib/A.so", OpenNow | OpenNoLoad,
                    bias + page, hooks, &first));
    CHECK(starts == 18 && !api.close(api.context, first, hooks));
    _exit(0);
  }
  int api_status;
  CHECK(waitpid(api_child, &api_status, 0) == api_child && api_status == 0);
  RuntimeLoad load(page);
  auto prepare = [&]() {
    return load.prepare(registry, scopes, *caller, text("A.so"), environment,
                        false);
  };
  rewind();
  CHECK(!prepare().error && load.view());
  const unsigned baseline = calls;
  CHECK(scopes.snapshot() == before && registry.snapshot() == before->registry);
  const auto *candidate = load.view()->registry;
  CHECK(candidate->images.size() == 3);
  const auto &tls = candidate->images[1]->tls;
  CHECK(tls.module_id > 1 && tls.generation > 1 && !tls.tp_offset &&
        tls.file_size == 8 && tls.memory_size == 16);
  Address pointer = 0;
  for (unsigned i = 0; i < 8; ++i)
    pointer = (pointer << 8) |
              reinterpret_cast<const unsigned char *>(tls.template_address)[i];
  CHECK(pointer == candidate->images[1]->elf.load_bias() + 0x2004);
  CHECK(!load.abort() && live() == resident_live);
  for (unsigned fail = 1; fail <= baseline; ++fail) {
    rewind();
    fail_at = fail;
    auto result = prepare();
    CHECK(result.error || result.cleanup_error);
    CHECK(!load.view() && scopes.snapshot() == before &&
          registry.snapshot() == before->registry);
    CHECK(!load.abort() && live() == resident_live && opens == closes);
  }
  // A TPOFF relocation is forbidden even without the STATIC_TLS marker.
  rewind();
  a.put(0xb08, 8, 46);
  a.set(DT_RELACOUNT, 0);
  CHECK(prepare().error == -ENOEXEC && !load.view());
  CHECK(live() == resident_live && scopes.snapshot() == before);
  a.put(0xb08, 8, 41);
  a.set(DT_RELACOUNT, 4);
  // A GD module relocation cannot carry an addend. This is structurally
  // admitted metadata but fails the relocation value preflight.
  rewind();
  a.put(0xb08, 8, 44);
  a.set(DT_RELACOUNT, 0);
  CHECK(prepare().error == -ENOEXEC && live() == resident_live);
  a.put(0xb08, 8, 41);
  a.set(DT_RELACOUNT, 4);
  rewind();
  a.add(DT_FLAGS, DF_STATIC_TLS);
  a.finish();
  CHECK(prepare().error == -ENOEXEC && live() == resident_live);
  a.set(DT_FLAGS, 0);
  // Malformed TLS layout and unsupported relocation records never publish.
  rewind();
  a.put(ph(4, 40), 8, 4);
  CHECK(prepare().error == -ENOEXEC && live() == resident_live);
  a.put(ph(4, 40), 8, 16);
  rewind();
  a.put(0xb08, 8, 999);
  CHECK(prepare().error == -ENOEXEC && live() == resident_live);
  a.put(0xb08, 8, 41);
  rewind();
  a.put(ph(2, 4), 4, PF_R | PF_W | PF_X);
  CHECK(prepare().error == -ENOEXEC && live() == resident_live);
  a.put(ph(2, 4), 4, PF_R | PF_X);
  // Undefined strong foo cannot bind to hidden definitions in other images.
  rewind();
  a.put(0xb08, 8, (Address(1) << 32) | 42);
  a.set(DT_RELACOUNT, 0);
  a.put(0x81e, 2, SHN_UNDEF);
  a.put(0x820, 8, 0);
  main_file.bytes[0x81d] = b.bytes[0x81d] = STV_HIDDEN;
  CHECK(prepare().error == -ENOEXEC && live() == resident_live);
  main_file.bytes[0x81d] = b.bytes[0x81d] = 0;
  // The globally selected main definition is not a DT_NEEDED edge of A.
  // Record the real interposer once despite both relocation passes.
  rewind();
  CHECK(!prepare().error);
  const auto *bound = load.view()->registry->images[1];
  CHECK(bound->bindings && bound->bindings->provider == caller &&
        !bound->bindings->next);
  for (const auto *dependency : bound->dependencies)
    CHECK(dependency != caller);
  CHECK(!caller->bindings);
  CHECK(!load.abort() && live() == resident_live && !caller->bindings);
  // Weak-zero bindings have no code/data provider to retain.
  rewind();
  main_file.bytes[0x81d] = b.bytes[0x81d] = STV_HIDDEN;
  a.bytes[0x81c] = (STB_WEAK << 4) | STT_FUNC;
  CHECK(!prepare().error && !load.view()->registry->images[1]->bindings);
  CHECK(!load.abort() && live() == resident_live);
  main_file.bytes[0x81d] = b.bytes[0x81d] = 0;
  a.bytes[0x81c] = (STB_GLOBAL << 4) | STT_FUNC;
  a.put(0x81e, 2, 1);
  a.put(0x820, 8, 0x2000);
  a.put(0xb08, 8, 41);
  a.set(DT_RELACOUNT, 4);
  // Cleanup failure stays owned and prevents transaction reuse.
  rewind();
  CHECK(!prepare().error);
  fail_unmaps = true;
  CHECK(load.abort() == -EIO && !load.view());
  CHECK(prepare().error == -EBUSY);
  fail_unmaps = false;
  CHECK(!load.abort() && live() == resident_live);
  // A separately committed scope update makes this prepared load stale.
  rewind();
  CHECK(!prepare().error);
  RuntimeScopes::Transaction other(page);
  uint64_t token;
  CHECK(!scopes.prepare_open(*before->registry, nullptr, false, other, token));
  CHECK(!scopes.publish(other));
  CHECK(load.commit(token) == -EAGAIN && !token);
  CHECK(!load.abort() && registry.snapshot() == before->registry);
  // The old registry is still valid; the final release-store publishes all
  // late records and fully relocated templates together, without syscalls.
  a.put(0xb08, 8, (Address(1) << 32) | 42);
  a.set(DT_RELACOUNT, 0);
  // This fixture reuses the first array word as a TLS/interposition probe;
  // it is not a callable initializer in the final committed image.
  a.set(DT_INIT_ARRAYSZ, 0);
  CHECK(!prepare().error);
  unsigned prior_calls = calls;
  CHECK(!load.commit(token) && token && calls == prior_calls);
  uint64_t first_handle = token;
  CHECK(scopes.snapshot()->registry == registry.snapshot());
  CHECK(scopes.snapshot()->unwind &&
        scopes.snapshot()->unwind->registry == registry.snapshot());
  CHECK(registry.snapshot()->images.size() == 3 && before->registry->images.size() == 1);
  CHECK(load.abort() == -EBUSY);
  RuntimeLoad reopen(page);
  const auto *published = registry.snapshot();
  const auto *original_bindings = published->images[1]->bindings;
  CHECK(original_bindings && original_bindings->provider == caller &&
        !original_bindings->next);
  const Address template_address = published->images[1]->tls.template_address;
  unsigned maps = mapped_files;
  CHECK(!reopen.prepare(registry, scopes, *caller, text("A.so"), environment,
                        true, true).error);
  CHECK(!reopen.commit(token) && registry.snapshot() == published &&
        mapped_files == maps && published->images[1]->tls.template_address == template_address);
  CHECK(published->images[1]->bindings == original_bindings);
  // Private retirement uses the real committed graph/mapping owners. Keep
  // publication descriptors stable while releasing their backing metadata.
  RuntimeInitializers initialization;
  RuntimeInitializers::Batch boot(page), late(page);
  LoaderLocks locks;
  LoaderCancellation cancellation{
      [](uint64_t *saved) -> long { *saved = 0; return 0; },
      [](uint64_t) -> long { return 0; }};
  LoaderLocks::Session session(locks, cancellation);
  InitialStack stack{};
  stack.valid = true;
  RuntimeQueries queries(page);
  RetirementEvents events(nullptr, [](void *) {});
  RuntimeRetirement retirement(page, events, scopes, queries, initialization);
  const ImageRegistry::Image *removed[] = {published->images[1], published->images[2]};
  Address biases[] = {removed[0]->elf.load_bias(), removed[1]->elf.load_bias()};
  {
    LoaderLocks::Guard guard(session, LoaderLocks::Kind::Mutation);
    CHECK(!initialization.prepare(*before->registry, boot, true));
    CHECK(!initialization.attach(boot, *before->registry));
    CHECK(!initialization.adopt_startup(boot, before->registry->images));
    CHECK(!initialization.prepare(*published, late));
    CHECK(!initialization.attach(late, *published));
    CHECK(!initialization.run(late, session, stack,
                              [](Address, int, char **, char **) {}));
    CHECK(!queries.prepare(*published));
    queries.publish();
    CHECK(!retirement.close(first_handle, session));
    CHECK(!retirement.close(token, session));
  }
  RuntimeLoad *owners[] = {&load, &reopen};
  pid_t reader_child = fork();
  CHECK(reader_child >= 0);
  if (!reader_child) {
    auto reader = scopes.acquire();
    CHECK(!retirement.drain(session, [](Address) {}));
    CHECK(retirement.removals()->state ==
          RuntimeRetirement::Removal::WaitingReaders);
    LoaderLocks::Guard guard(session, LoaderLocks::Kind::Mutation);
    CHECK(!retirement.reclaim(registry, owners, session));
    CHECK(owner(biases[0], 1) && owner(biases[1], 1));
    _exit(0);
  }
  int reader_status;
  CHECK(waitpid(reader_child, &reader_status, 0) == reader_child &&
        reader_status == 0);
  CHECK(!retirement.drain(session, [](Address) {}));
  CHECK(retirement.removals() &&
        retirement.removals()->state == RuntimeRetirement::Removal::Ready);
  {
    LoaderLocks::Guard guard(session, LoaderLocks::Kind::Mutation);
    // Fork the model so every syscall boundary starts from the same Ready
    // ownership state. The model rejects duplicate unmaps of successful ranges.
    unsigned begin = calls;
    pid_t baseline_child = fork();
    CHECK(baseline_child >= 0);
    if (!baseline_child) {
      CHECK(!retirement.reclaim(registry, owners, session));
      CHECK(calls - begin < 250);
      _exit(calls - begin);
    }
    int baseline_status;
    CHECK(waitpid(baseline_child, &baseline_status, 0) == baseline_child);
    CHECK(WIFEXITED(baseline_status));
    unsigned release_calls = WEXITSTATUS(baseline_status);
    CHECK(release_calls);
    printf("reclamation: %u syscall failure positions\n", release_calls);
    fflush(stdout);
    for (unsigned fail = 1; fail <= release_calls; ++fail) {
      pid_t child = fork();
      CHECK(child >= 0);
      if (!child) {
        fail_at = calls + fail;
        CHECK(retirement.reclaim(registry, owners, session) < 0);
        fail_at = 0;
        CHECK(!retirement.reclaim(registry, owners, session));
        CHECK(retirement.removals()->state == RuntimeRetirement::Removal::Reclaimed);
        CHECK(registry.snapshot()->images.size() == 1 && caller->elf.header());
        _exit(0);
      }
      int status;
      CHECK(waitpid(child, &status, 0) == child && status == 0);
    }
    // Fail the second image's reservation release after the first mapping
    // and its private metadata have already been released.
    fail_release_address = biases[1];
    CHECK(retirement.reclaim(registry, owners, session) == -EIO);
    CHECK(retirement.removals()->state == RuntimeRetirement::Removal::Quarantined);
    CHECK(registry.snapshot()->images.size() == 1);
    CHECK(!owner(biases[0], 1) && owner(biases[1], 1));
    CHECK(load.collect_storage() == -EBUSY);
    CHECK(removed[0]->ownership.admission() == ModuleOwnership::State::Reclaimed);
    CHECK(!removed[1]->ownership.available_for_load());
    fail_release_address = 0;
    CHECK(!retirement.reclaim(registry, owners, session));
    CHECK(retirement.removals()->state == RuntimeRetirement::Removal::Reclaimed);
    CHECK(!owner(biases[0], 1) && !owner(biases[1], 1));
    CHECK(removed[0]->path.empty() && removed[1]->dependencies.empty());
    CHECK(!removed[0]->elf.header() && !removed[1]->elf.header());
    CHECK(registry.snapshot()->images[0] == caller && caller->elf.header());
    CHECK(!retirement.reclaim(registry, owners, session));
  }
  printf("late transaction: %u syscall failure positions, TLS and metadata rejection, rollback and coherent commit passed\n", baseline);
  return 0;
}

extern "C" int main() {
  pid_t load_child = fork();
  CHECK(load_child >= 0);
  if (!load_child)
    _exit(runtime_load_tests());
  int load_status;
  CHECK(waitpid(load_child, &load_status, 0) == load_child && load_status == 0);
  pid_t child = fork();
  CHECK(child >= 0);
  if (!child)
    _exit(runtime_tests());
  int status;
  CHECK(waitpid(child, &status, 0) == child && status == 0);
  CHECK(startup_tests() == 0);
  {
    // Unknown main identity cannot collide with a real all-zero file identity.
    // DSO aliases still deduplicate when main provenance is absent.
    Library anonymous(ImageRole::MainPIE), dependency;
    anonymous.need("A.so");
    anonymous.need("alias.so");
    MappedRegion regions[] = {{0, Bytes(anonymous.bytes, page)},
                             {page, Bytes(anonymous.bytes + page, page)},
                             {2 * page, Bytes(anonymous.bytes + 2 * page, page)}};
    ElfImage image;
    CHECK(ElfImage::open_mapped(regions, 0, 64, ImageRole::MainPIE, bias, page,
                                image) == ElfError::None);
    files[0] = {"/lib/A.so", &dependency, {0, 0, 0}};
    files[1] = {"/lib/alias.so", &dependency, {0, 0, 0}};
    file_count = 2;
    SearchEnvironment environment;
    CHECK(!environment.parse(false, {}, {}));
    start();
    StartupGraph graph(page);
    CHECK(!graph.build(image, nullptr, {}, environment).error);
    CHECK(graph.size() == 2 && graph.scope()->path().empty() && mapped_files == 1);
    CHECK(graph.scope()->dependencies()->target ==
          graph.scope()->dependencies()->next->target);
    CHECK(!graph.reset() && !live() && opens == closes);
  }
  Library main_file(ImageRole::MainPIE), a, b, c, d;
  main_file.need("A.so");
  main_file.need("B.so");
  main_file.need("alias.so");
  main_file.need("D.so");
  a.need("C.so");
  b.need("C.so");
  c.need("A.so");
  c.need("main");
  files[0] = {"/lib/A.so", &a, {2, 1, 0}};
  files[1] = {"/lib/B.so", &b, {3, 1, 0}};
  files[2] = {"/lib/C.so", &c, {4, 1, 0}};
  files[3] = {"/lib/alias.so", &a, {2, 1, 0}};
  files[4] = {"/lib/D.so", &d, {2, 2, 0}};
  files[5] = {"/lib/main", &main_file, {1, 1, 0}};
  file_count = 6;
  MappedRegion regions[] = {
      {0, Bytes(main_file.bytes, page)},
      {page, Bytes(main_file.bytes + page, page)},
      {2 * page, Bytes(main_file.bytes + 2 * page, page)}};
  ElfImage main;
  CHECK(ElfImage::open_mapped(regions, 0, 64, ImageRole::MainPIE, bias, page,
                              main) == ElfError::None);
  SearchEnvironment environment;
  CHECK(environment.parse(false, {}, text("/lib")) == 0);
  StartupGraph graph(page);
  start();
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  unsigned baseline = calls;
  CHECK(graph.size() == 5 && mapped_files == 4 && opens == 8 &&
        closes == opens);
  const StartupGraph::Node *nodes[5];
  const char *paths[] = {"/lib/main", "/lib/A.so", "/lib/B.so", "/lib/D.so",
                         "/lib/C.so"};
  unsigned n = 0;
  for (const auto *node = graph.scope(); node; node = node->next()) {
    CHECK(n < 5);
    nodes[n] = node;
    CHECK(node->fully_discovered());
    CHECK(node->path().size() == strlen(paths[n]));
    CHECK(!memcmp(node->path().data(), paths[n], node->path().size()));
    ++n;
  }
  CHECK(n == 5 && nodes[0]->image() == &main);
  Arena symbol_storage(page);
  StartupSymbols symbols;
  CHECK(StartupSymbols::open(graph, symbol_storage, symbols) == 0);
  SymbolValue bound;
  CHECK(symbols.resolve(4, 1, bound) == SymbolError::None);
  CHECK(bound.object == 0 && bound.address == bias + 0x2000);
  CHECK(symbol_storage.reset() == 0);
  auto *edge = nodes[0]->dependencies();
  CHECK(edge->target == nodes[1]);
  edge = edge->next;
  CHECK(edge->target == nodes[2]);
  edge = edge->next;
  CHECK(edge->target == nodes[1]);
  edge = edge->next;
  CHECK(edge->target == nodes[3] && !edge->next);
  CHECK(nodes[1]->dependencies()->target == nodes[4]);
  CHECK(nodes[2]->dependencies()->target == nodes[4]);
  CHECK(nodes[4]->dependencies()->target == nodes[1]);
  CHECK(nodes[4]->dependencies()->next->target == nodes[0]);
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        -EBUSY);
  CHECK(graph.reset() == 0 && !live() && !fd_live && !graph.scope());
  CHECK(main.header() && main_file.bytes[0] == 0x7f && graph.reset() == 0);

  start();
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  unsigned before_protection = calls;
  CHECK(graph.finalize_protections().error == -EINVAL);
  CHECK(graph.relocate().error == 0);
  before_protection = calls;
  CHECK(graph.finalize_protections().error == 0);
  unsigned protection_calls = calls - before_protection;
  CHECK(protection_calls == 20);
  CHECK(graph.finalize_protections().error == 0);
  CHECK(calls == before_protection + protection_calls);
  CHECK(graph.reset() == 0 && !live());
  for (unsigned fail = 1; fail <= protection_calls; ++fail) {
    start();
    CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
          0);
    CHECK(graph.relocate().error == 0);
    fail_at = calls + fail;
    auto result = graph.finalize_protections();
    CHECK(result.error == -EIO && !result.cleanup_error);
    CHECK(!graph.scope() && !live() && main_file.bytes[0] == 0x7f);
    CHECK(graph.finalize_protections().error == -EINVAL);
  }
  start();
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  CHECK(graph.relocate().error == 0);
  fail_at = calls + 20;
  fail_unmaps = true;
  auto protection_failure = graph.finalize_protections();
  CHECK(protection_failure.error == -EIO &&
        protection_failure.cleanup_error == -EIO);
  CHECK(!graph.scope() && live());
  fail_unmaps = false;
  CHECK(graph.reset() == 0 && !live() && main_file.bytes[0] == 0x7f);

  start();
  files[4].identity = {2, 1, 1};
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  CHECK(graph.size() == 5 && mapped_files == 4);
  CHECK(graph.reset() == 0 && !live());
  files[4].identity = {2, 2, 0};
  start();
  StartupGraph wrong_page(4096);
  CHECK(
      wrong_page.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
      -EINVAL);
  CHECK(!calls);
  CHECK(
      graph.build(main, {1, 1, 0}, text("relative-main"), environment).error ==
      -EINVAL);
  CHECK(!live());
  SearchEnvironment rejected;
  start();
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), rejected).error ==
        -EINVAL);
  CHECK(!calls);

  // Includes graph storage, fd identity, mapper acquisitions, edge/ancestry
  // allocation and final temporary releases, after several successful DSOs.
  for (unsigned fail = 1; fail <= baseline; ++fail) {
    start();
    fail_at = fail;
    MapResult result =
        graph.build(main, {1, 1, 0}, text("/lib/main"), environment);
    CHECK(result.error == -EIO && !graph.scope() && graph.size() == 0);
    CHECK(graph.reset() == 0 && !live() && !fd_live && opens == closes);
    CHECK(main_file.bytes[0] == 0x7f);
  }
  start();
  interrupt_stat = true;
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  CHECK(graph.reset() == 0 && !live());
  start();
  no_identity = true;
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        -ENOEXEC);
  CHECK(!live() && !fd_live && !mapped_files);
  start();
  fail_at = 8;
  fail_unmaps = true;
  MapResult result =
      graph.build(main, {1, 1, 0}, text("/lib/main"), environment);
  CHECK(result.error == -EIO && result.cleanup_error == -EIO && live());
  CHECK(!graph.scope() && graph.reset() == -EIO);
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        -EBUSY);
  fail_unmaps = false;
  CHECK(graph.reset() == 0 && !live() && !fd_live);
  start();
  files[2].path = "/missing/C.so";
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        -ENOENT);
  CHECK(mapped_files == 3 && !live() && !graph.scope() && opens == closes);
  files[2].path = "/lib/C.so";

  main_file.initializers(true);
  a.initializers();
  b.initializers();
  c.initializers();
  d.initializers();
  arguments.valid = true;
  arguments.argc = 2;
  arguments.argv = 0x8000;
  arguments.envp = 0x9000;
  active_graph = &graph;
  // Dependency cycle A/C is emitted in BFS order, before B which needs C.
  // Back-edges to main do not move main initialization before dependencies.
  start();
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  Arena init_storage(page);
  StartupInitializers initializers;
  active_initializers = &initializers;
  CHECK(initializers.prepare(graph, init_storage) == -EINVAL);
  CHECK(graph.relocate().error == 0);
  CHECK(initializers.prepare(graph, init_storage) == -EINVAL);
  CHECK(graph.finalize_protections().error == 0);
  CHECK(initializers.prepare(graph, init_storage) == 0);
  CHECK(initializers.prepare(graph, init_storage) == -EBUSY);
  CHECK(initializers.run(arguments, record_init) == 0);
  CHECK(trace_count == 16 && trace[0] == bias + 0x200c);
  const char *init_paths[] = {"/lib/A.so", "/lib/C.so", "/lib/B.so",
                              "/lib/D.so", "/lib/main"};
  CHECK(initializers.completed_count() == 5 &&
        !initializers.completed_object(5));
  for (unsigned i = 0; i < 5; ++i) {
    const auto *node = initializers.completed_object(i);
    CHECK(node->path().size() == strlen(init_paths[i]));
    CHECK(!memcmp(node->path().data(), init_paths[i], node->path().size()));
    for (unsigned j = 0; j < 3; ++j)
      CHECK(trace[1 + i * 3 + j] ==
            node->image()->load_bias() + 0x2000 + 4 * j);
  }
  CHECK(initializers.run(arguments, record_init) == 0 && trace_count == 16);
  StartupInitializers duplicate;
  CHECK(duplicate.prepare(graph, init_storage) == -EBUSY);
  initializers.finalize(record_fini);
  CHECK(trace_count == 31);
  for (unsigned i = 0; i < 5; ++i) {
    Address base = initializers.completed_object(4 - i)->image()->load_bias();
    CHECK(trace[16 + 3 * i] == base + 0x2014);
    CHECK(trace[17 + 3 * i] == base + 0x2010);
    CHECK(trace[18 + 3 * i] == base + 0x2018);
  }
  initializers.finalize(record_fini);
  CHECK(trace_count == 31 && initializers.run(arguments, record_init) == -ECANCELED);
  CHECK(init_storage.reset() == 0 && graph.reset() == 0 && !live());
  trace_count = 0;

  const size_t partial_stops[] = {1, 5};
  for (size_t stop : partial_stops) {
    start();
    CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error == 0);
    CHECK(graph.relocate().error == 0 && graph.finalize_protections().error == 0);
    StartupInitializers partial;
    active_initializers = &partial;
    CHECK(partial.prepare(graph, init_storage) == 0);
    stop_after = stop;
    CHECK(partial.run(arguments, stop_initialization) == -ECANCELED);
    CHECK(partial.completed_count() == (stop == 1 ? 0 : 1));
    CHECK(trace_count == (stop == 1 ? 1 : 8));
    partial.finalize(record_fini);
    CHECK(trace_count == (stop == 1 ? 1 : 8));
    CHECK(init_storage.reset() == 0 && graph.reset() == 0 && !live());
    trace_count = 0;
  }

  const Address invalid_callbacks[] = {0, ~Address(0), bias + 0x2001,
                                       bias + 0x4000, Address(1) << 43};
  const Address callback_slots[] = {0x4900, 0x4940};
  for (Address address : invalid_callbacks) {
    for (Address slot : callback_slots) {
      start();
      CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error == 0);
      CHECK(graph.relocate().error == 0);
      // Bad relocated init/fini pointers reject the whole callback plan.
      main_file.put(slot, 8, address);
      CHECK(graph.finalize_protections().error == 0);
      StartupInitializers bad;
      CHECK(bad.prepare(graph, init_storage) == -ENOEXEC);
      CHECK(bad.run(arguments, record_init) == -EINVAL && !trace_count);
      bad.finalize(record_fini);
      CHECK(!trace_count);
      CHECK(init_storage.reset() == 0 && graph.reset() == 0 && !live());
    }
  }
  start();
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  CHECK(graph.relocate().error == 0);
  fail_at = calls + 1;
  CHECK(graph.finalize_protections().error == -EIO);
  StartupInitializers unprotected;
  CHECK(unprotected.prepare(graph, init_storage) == -EINVAL && !trace_count);
  CHECK(graph.reset() == 0 && !live());

  start();
  a.put(0x81e, 2, SHN_UNDEF);
  a.put(0x820, 8, 0);
  a.bytes[0x81d] = STV_HIDDEN;
  a.put(0xb20, 8, (Address(1) << 32) | 42);
  a.set(DT_RELACOUNT, 1);
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  CHECK(graph.relocate().error == -ENOEXEC);
  StartupInitializers unresolved;
  CHECK(unresolved.prepare(graph, init_storage) == -EINVAL && !trace_count &&
        !live());
  a.put(0x81e, 2, 1);
  a.put(0x820, 8, 0x2000);
  a.bytes[0x81d] = STV_DEFAULT;
  a.put(0xb20, 8, 41);
  a.set(DT_RELACOUNT, 2);

  start();
  // Explicit relative dependencies use the actual startup cwd, not a host path.
  Library relative_main(ImageRole::MainPIE);
  relative_main.need("./A.so");
  for (unsigned i = 0; i < 3; ++i)
    regions[i].bytes = Bytes(relative_main.bytes + i * page, page);
  CHECK(ElfImage::open_mapped(regions, 0, 64, ImageRole::MainPIE, bias, page,
                              main) == ElfError::None);
  files[0].path = "/lib/./A.so";
  files[2].path = "/lib/./C.so";
  files[5].path = "/lib/./main";
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  CHECK(graph.size() == 3 && mapped_files == 2);
  CHECK(graph.reset() == 0 && !live());

  Library diamond(ImageRole::MainPIE), left, right, leaf;
  diamond.need("left.so");
  diamond.need("right.so");
  left.need("leaf.so");
  right.need("leaf.so");
  diamond.initializers(true);
  left.initializers();
  right.initializers();
  leaf.initializers();
  // The last left initializer is a preemptible pointer resolved into main.
  left.set(DT_RELACOUNT, 1);
  left.put(0xb20, 8, (Address(1) << 32) | 42);
  left.put(0xb28, 8, 0);
  files[0] = {"/lib/left.so", &left, {2, 1, 0}};
  files[1] = {"/lib/right.so", &right, {3, 1, 0}};
  files[2] = {"/lib/leaf.so", &leaf, {4, 1, 0}};
  file_count = 3;
  for (unsigned i = 0; i < 3; ++i)
    regions[i].bytes = Bytes(diamond.bytes + i * page, page);
  CHECK(ElfImage::open_mapped(regions, 0, 64, ImageRole::MainPIE, bias, page,
                              main) == ElfError::None);
  start();
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  CHECK(graph.relocate().error == 0 && graph.finalize_protections().error == 0);
  StartupInitializers diamond_init;
  fail_at = calls + 1;
  CHECK(diamond_init.prepare(graph, init_storage) == -EIO && !trace_count);
  fail_at = 0;
  CHECK(diamond_init.prepare(graph, init_storage) == 0);
  active_initializers = &diamond_init;
  InitialStack invalid_arguments{};
  CHECK(diamond_init.run(invalid_arguments, record_init) == -EINVAL &&
        !trace_count);
  CHECK(diamond_init.run(arguments, record_init) == 0 && trace_count == 13);
  CHECK(trace[6] == bias + 0x2000);
  const char *diamond_paths[] = {"/lib/leaf.so", "/lib/left.so",
                                 "/lib/right.so", "/lib/main"};
  for (unsigned i = 0; i < 4; ++i) {
    const auto *node = diamond_init.completed_object(i);
    CHECK(node && node->path().size() == strlen(diamond_paths[i]));
    CHECK(!memcmp(node->path().data(), diamond_paths[i], node->path().size()));
  }
  CHECK(init_storage.reset() == 0 && graph.reset() == 0 && !live());
  trace_count = 0;
  Library empty_main(ImageRole::MainPIE);
  for (unsigned i = 0; i < 3; ++i)
    regions[i].bytes = Bytes(empty_main.bytes + i * page, page);
  CHECK(ElfImage::open_mapped(regions, 0, 64, ImageRole::MainPIE, bias, page,
                              main) == ElfError::None);
  start();
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  CHECK(graph.relocate().error == 0 && graph.finalize_protections().error == 0);
  StartupInitializers empty_init;
  CHECK(empty_init.prepare(graph, init_storage) == 0);
  CHECK(empty_init.run(arguments, record_init) == 0 && !trace_count);
  CHECK(empty_init.completed_count() == 1);
  CHECK(init_storage.reset() == 0 && graph.reset() == 0 && !live());
  puts("loader graph, rollback, relocation/protection gating and initializer "
       "traces passed");
  // Exercise the composed production services without executing MMIX code.
  start();
  empty_main.add(DT_DEBUG, 0);
  empty_main.finish();
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
        0);
  CHECK(graph.relocate().error == 0);
  RuntimeState state(page, arguments, graph.relocated_tls());
  CHECK(state.prepare(graph, environment, 0x123000) == 0);
  CHECK(graph.finalize_protections().error == 0);
  CHECK(state.publish() == 0);
  LoaderServices services{};
  services.size = sizeof(services);
  services.tls_set = graph.relocated_tls().modules();
  state.services(services);
  CHECK(has_reclaiming_services(services));
  auto incompatible = services;
  incompatible.size -= sizeof(uint64_t);
  CHECK(!has_reclaiming_services(incompatible));
  incompatible = services;
  incompatible.lifetime = LoaderLifetime::Unspecified;
  CHECK(!has_reclaiming_services(incompatible));
  incompatible.lifetime = static_cast<LoaderLifetime>(~uint64_t(0));
  CHECK(!has_reclaiming_services(incompatible));
  // Every endpoint is required before libc publishes any adopted services.
#define CHECK_MISSING_SERVICE(member)                                           \
  incompatible = services;                                                    \
  incompatible.member = nullptr;                                              \
  CHECK(!has_reclaiming_services(incompatible))
  CHECK_MISSING_SERVICE(context);
  CHECK_MISSING_SERVICE(tls_set);
  CHECK_MISSING_SERVICE(prepare_thread_tls);
  CHECK_MISSING_SERVICE(release_thread_tls);
  CHECK_MISSING_SERVICE(resolve_startup_tls);
  CHECK_MISSING_SERVICE(finalize_startup);
  CHECK_MISSING_SERVICE(find_unwind_image);
  CHECK_MISSING_SERVICE(release_unwind_image);
  CHECK_MISSING_SERVICE(acquire_unwind_image);
  CHECK_MISSING_SERVICE(retain_unwind_image);
  CHECK_MISSING_SERVICE(drop_unwind_image);
  CHECK_MISSING_SERVICE(lookup_unwind_lease);
  CHECK_MISSING_SERVICE(pin_unwind_lease);
  CHECK_MISSING_SERVICE(api.context);
  CHECK_MISSING_SERVICE(api.open);
  CHECK_MISSING_SERVICE(api.lookup);
  CHECK_MISSING_SERVICE(api.close);
  CHECK_MISSING_SERVICE(queries.context);
  CHECK_MISSING_SERVICE(queries.address);
  CHECK_MISSING_SERVICE(queries.info);
  CHECK_MISSING_SERVICE(queries.iterate);
  CHECK_MISSING_SERVICE(process.context);
  CHECK_MISSING_SERVICE(process.prepare_fork);
  CHECK_MISSING_SERVICE(process.finish_fork);
  CHECK_MISSING_SERVICE(run_initializers);
  CHECK_MISSING_SERVICE(images.context);
  CHECK_MISSING_SERVICE(images.retain);
  CHECK_MISSING_SERVICE(images.release);
  CHECK_MISSING_SERVICE(images.retain_process);
  CHECK_MISSING_SERVICE(signals.context);
  CHECK_MISSING_SERVICE(signals.retain);
  CHECK_MISSING_SERVICE(signals.finish);
#undef CHECK_MISSING_SERVICE
  LoaderCancellation hooks{[](uint64_t *saved) -> long {
                             *saved = 0;
                             return 0;
                           },
                           [](uint64_t) -> long { return 0; }};
  CHECK(services.api.context && services.queries.context &&
        services.process.context);
  CHECK(services.prepare_thread_tls && services.release_thread_tls &&
        services.resolve_startup_tls && services.find_unwind_image &&
        services.release_unwind_image && services.acquire_unwind_image &&
        services.retain_unwind_image && services.drop_unwind_image);
  UnwindImageView invalid_view{};
  CHECK(services.acquire_unwind_image(services.context, 1, &invalid_view) ==
            ENOENT &&
        !invalid_view.token);
  CHECK(services.retain_unwind_image(services.context, 0, &invalid_view) ==
            EINVAL &&
        !invalid_view.token);
  CHECK(services.drop_unwind_image(services.context, 0) == EINVAL);
  CHECK(services.run_initializers(services.context, hooks) == 0);
  CHECK(services.run_initializers(services.context, hooks) == 0);
  uint64_t handle = 0;
  CHECK(!services.api.open(services.api.context, nullptr, OpenNow,
                           bias + 0x2000, hooks, &handle) &&
        handle);
  link_map *map = nullptr;
  CHECK(!services.queries.info(services.queries.context, handle, InfoLinkMap,
                               &map, hooks) &&
        map);
  CHECK(runtime_debug_record()->r_map == map &&
        runtime_debug_record()->r_ldbase == 0x123000);
  CHECK(!services.api.close(services.api.context, handle, hooks));
  CHECK(!services.process.prepare_fork(services.process.context, nullptr));
  services.process.finish_fork(services.process.context, false, false);
  services.finalize_startup(services.context, hooks);
  services.finalize_startup(services.context, hooks);
  puts("resident startup service composition passed");
  return 0;
}
