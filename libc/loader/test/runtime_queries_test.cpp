//===-- Runtime query retirement tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/runtime_queries.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace loader_test;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    std::fprintf(stderr, "query retirement check at %d\n", line);
    std::abort();
  }
}
static bool fail_allocation;
namespace LIBC_NAMESPACE::loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  if (fail_allocation && n == SYS_mmap)
    return -ENOMEM;
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
[[noreturn]] void fatal_startup() { _exit(127); }
} // namespace LIBC_NAMESPACE::loader

struct Input : DynamicFixture {
  MappedRegion region;
  ImageRegistry::Image image{};
  explicit Input(Address address) {
    region = {0, view()};
    check(ElfImage::open_mapped({&region, 1}, 0, 64,
                               ImageRole::SharedObject, address, page,
                               image.elf) == ElfError::None);
    check(DynamicInfo::open(image.elf, image.dynamic) == DynamicError::None);
    static const unsigned char name[] = "libquery.so";
    image.path = {name, sizeof(name) - 1};
    image.discovered = true;
  }
};

static r_debug debug{};
static unsigned notices;
static unsigned expected_state, old_size, new_size;
static void notification() {
  check(unsigned(debug.r_state) == (notices % 2 ? 0 : expected_state));
  size_t size = 0;
  link_map *previous = nullptr;
  for (auto *map = debug.r_map; map; map = map->l_next) {
    check(map->l_prev == previous && map->l_name && map->l_ld);
    previous = map;
    ++size;
  }
  check(size == (notices % 2 ? new_size : old_size));
  ++notices;
}

static void defined_symbol_visibility() {
  const unsigned bindings[] = {STB_LOCAL, STB_GLOBAL, STB_WEAK};
  const unsigned visibilities[] = {STV_DEFAULT, STV_INTERNAL, STV_HIDDEN,
                                  STV_PROTECTED};
  const unsigned types[] = {STT_NOTYPE, STT_OBJECT, STT_FUNC};
  for (unsigned type : types) {
    for (unsigned binding : bindings) {
      for (unsigned visibility : visibilities) {
        Input input(0x900000);
        input.bytes[0x81c] = (binding << 4) | type;
        input.bytes[0x81d] = visibility;
        check(DynamicInfo::open(input.image.elf, input.image.dynamic) ==
              DynamicError::None);
        const ImageRegistry::Image *images[] = {&input.image};
        ImageRegistry::Snapshot view{1, images};
        RuntimeQueries queries(page);
        check(!queries.prepare(view));
        queries.publish();
        Dl_info info{};
        check(!RuntimeQueries::address(*queries.snapshot(), 0x902001, info));
        check(info.dli_sname && !std::strcmp(info.dli_sname, "foo"));
        check(info.dli_saddr == reinterpret_cast<void *>(0x902000));
      }
    }
  }
}

extern "C" int main() {
  defined_symbol_visibility();
  Input main(0x100000), late(0x300000), fresh(0x500000);
  const ImageRegistry::Image *one[] = {&main.image};
  const ImageRegistry::Image *two[] = {&main.image, &late.image};
  const ImageRegistry::Image *reloaded[] = {&main.image, &fresh.image};
  ImageRegistry::Snapshot initial{1, one}, loaded{2, two}, removed{3, one},
      reload{4, reloaded};
  RuntimeQueries queries(page, &debug, nullptr, notification);
  auto publish = [&](const auto &view, unsigned state, unsigned before,
                     unsigned after) {
    expected_state = state;
    old_size = before;
    new_size = after;
    unsigned previous = notices;
    check(!queries.prepare(view));
    queries.publish();
    check(notices == previous + 2);
  };
  publish(initial, 1, 0, 1);
  Dl_info main_address{};
  check(!RuntimeQueries::address(*queries.snapshot(), 0x102000, main_address));
  check(!std::strcmp(main_address.dli_fname, "libquery.so"));
  check(queries.snapshot()->records[0].map.l_name[0] == 0);
  // Main image discovery can legitimately have no verified executable path.
  Input unnamed(0x700000);
  unnamed.image.path = {};
  const ImageRegistry::Image *unknown_images[] = {&unnamed.image};
  ImageRegistry::Snapshot unknown{1, unknown_images};
  RuntimeQueries unknown_queries(page);
  check(!unknown_queries.prepare(unknown));
  unknown_queries.publish();
  check(RuntimeQueries::address(*unknown_queries.snapshot(), 0x702000,
                                 main_address) == ENOENT);
  check(!main_address.dli_fname && !main_address.dli_fbase);

  // Replacing a prepared addition with the committed view cancels the addition.
  check(!queries.prepare(loaded));
  check(!queries.prepare(initial));
  queries.publish();
  check(notices == 2 && queries.snapshot()->records.size() == 1);
  check(!queries.prepare(loaded));
  check(!queries.abort());
  queries.publish();
  check(notices == 2);
  fail_allocation = true;
  check(queries.prepare(loaded) == -ENOMEM);
  fail_allocation = false;
  check(!queries.abort());
  queries.publish();
  check(notices == 2);
  publish(loaded, 1, 1, 2);

  // A real reader stays in its old PHDR callback while the writer removes and
  // reloads images. Barriers establish the schedule without timing assumptions.
  struct Reader {
    RuntimeQueries *queries;
    const ImageRegistry::Image *image;
    pthread_barrier_t barrier;
  } reader{&queries, &late.image, {}};
  check(!pthread_barrier_init(&reader.barrier, nullptr, 2));
  pthread_t thread;
  check(!pthread_create(&thread, nullptr, [](void *p) -> void * {
    auto &r = *static_cast<Reader *>(p);
    auto lease = r.queries->acquire();
    pthread_barrier_wait(&r.barrier);
    pthread_barrier_wait(&r.barrier);
    auto nested = r.queries->acquire();
    check(!RuntimeQueries::record(*nested.get(), r.image));
    check(nested.get()->adds == 3 && nested.get()->subs == 1);
    const auto *record = RuntimeQueries::record(*lease.get(), r.image);
    check(record && record->map.l_prev && !record->map.l_next);
    dl_phdr_info info{};
    check(!RuntimeQueries::phdr_info(*lease.get(), *record, nullptr, nullptr, info));
    check(info.dlpi_adds == 2 && !info.dlpi_subs && info.dlpi_phnum);
    check(!std::strcmp(info.dlpi_name, "libquery.so"));
    Dl_info address{};
    check(!RuntimeQueries::address(*lease.get(), 0x302000, address));
    check(!std::strcmp(address.dli_sname, "foo"));
    return nullptr;
  }, &reader));
  pthread_barrier_wait(&reader.barrier);
  publish(removed, 2, 2, 1);
  check(!queries.readers_drained(&late.image));
  check(queries.collect_snapshots() == -EBUSY);
  Dl_info address{};
  check(RuntimeQueries::address(*queries.snapshot(), 0x302000, address) == ENOENT);
  publish(reload, 1, 1, 2);
  check(queries.snapshot()->adds == 3 && queries.snapshot()->subs == 1);
  check(!RuntimeQueries::address(*queries.snapshot(), 0x502000, address));
  check(!std::strcmp(address.dli_sname, "foo"));
  check(!queries.readers_drained(&late.image));
  pthread_barrier_wait(&reader.barrier);
  check(!pthread_join(thread, nullptr));
  check(!pthread_barrier_destroy(&reader.barrier));
  check(queries.readers_drained(&late.image));
  check(!queries.collect_snapshots());
  unsigned previous = notices;
  check(!queries.prepare(reload));
  queries.publish();
  check(notices == previous && queries.snapshot()->adds == 3 &&
        queries.snapshot()->subs == 1);
  // Escaped raw query pointers intentionally retain the generation.
  auto pinned = queries.acquire();
  pinned.pin();
  pinned = {};
  publish(removed, 2, 2, 1);
  check(!queries.readers_drained(&fresh.image));
  check(!queries.collect_snapshots());
  std::puts("query retirement, reload, readers and debugger publication passed");
  return 0;
}
