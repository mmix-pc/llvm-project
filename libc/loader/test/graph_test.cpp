//===-- Startup dependency graph tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/graph.h"
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
    assert(owner(a, b));
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
  files[4].identity = {2, 1, 1};
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), environment).error == 0);
  CHECK(graph.size() == 5 && mapped_files == 4);
  CHECK(graph.reset() == 0 && !live());
  files[4].identity = {2, 2, 0};
  start();
  StartupGraph wrong_page(4096);
  CHECK(wrong_page.build(main, {1, 1, 0}, text("/lib/main"), environment).error == -EINVAL);
  CHECK(!calls);
  CHECK(graph.build(main, {1, 1, 0}, text("relative-main"), environment).error == -EINVAL);
  CHECK(!live());
  SearchEnvironment rejected;
  start();
  CHECK(graph.build(main, {1, 1, 0}, text("/lib/main"), rejected).error == -EINVAL);
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
  puts("loader BFS graph, identity, cycles and transactional rollback passed");
  return 0;
}
