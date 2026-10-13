//===-- Startup dependency graph tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/graph.h"
#include "loader/linux/initialization.h"
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

using namespace loader_test;
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
static size_t file_position;
alignas(page) static unsigned char pool[8 * 1024 * 1024];
struct Allocation {
  Address address, size;
  bool reservation, live;
};
static Allocation allocations[128];
static size_t used;
static unsigned allocations_count;

static unsigned live() {
  unsigned count = 0;
  for (unsigned i = 0; i < allocations_count; ++i)
    count += allocations[i].live;
  return count;
}
static void start() {
  assert(!fd_live && !live());
  calls = fail_at = closes = opens = mapped_files = 0;
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
long raw_syscall(long number, long a, long b, long c, long d, long e, long f) {
  ++calls;
  if (number == SYS_close) {
    assert(fd_live && a == 7);
    fd_live = false;
    ++closes;
  }
  if (calls == fail_at)
    return -EIO;
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
           (Address(a) >= bias && Address(a + b) <= bias + 3 * page));
    assert((c & (PROT_EXEC | PROT_WRITE)) != (PROT_EXEC | PROT_WRITE));
    return 0;
  }
  if (number == SYS_munmap) {
    auto *range = owner(a, b);
    assert(range && range->address == Address(a) && range->size == Address(b));
    if (fail_unmaps)
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
static void record_init(Address address, int argc, char **argv, char **envp) {
  assert(trace_count < 32 && argc == 2 &&
         reinterpret_cast<Address>(argv) == arguments.argv &&
         reinterpret_cast<Address>(envp) == arguments.envp);
  assert(active_initializers->run(arguments, record_init) == -EBUSY);
  assert(active_graph->reset() == -EBUSY);
  trace[trace_count++] = address;
}

extern "C" int main() {
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
  CHECK(init_storage.reset() == 0 && graph.reset() == 0 && !live());
  trace_count = 0;

  const Address invalid_callbacks[] = {0, ~Address(0), bias + 0x2001,
                                       bias + 0x4000, Address(1) << 43};
  for (Address address : invalid_callbacks) {
    start();
    CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error ==
          0);
    CHECK(graph.relocate().error == 0);
    // Model a bad relocated pointer in the last object's initializer array.
    main_file.put(0x4900, 8, address);
    CHECK(graph.finalize_protections().error == 0);
    StartupInitializers bad;
    CHECK(bad.prepare(graph, init_storage) == -ENOEXEC);
    CHECK(bad.run(arguments, record_init) == -EINVAL && !trace_count);
    CHECK(init_storage.reset() == 0 && graph.reset() == 0 && !live());
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
  return 0;
}
