//===-- Loader self relocation tests --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "elf_fixture.h"
#include "loader/linux/self_relocation.h"
#include <stdio.h>

using namespace LIBC_NAMESPACE::loader;
using namespace loader_test;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                  \
      return 1;                                                                \
    }                                                                          \
  } while (false)

struct SelfFixture : Fixture {
  MappedRegion mapping;
  WritableRegion writable;
  ElfImage image;
  SelfFixture()
      : Fixture(ImageRole::Interpreter), mapping{0, view()},
        writable{0x4000, {bytes + 0x4000, 0x2000}} {
    segment(5, PT_DYNAMIC, PF_R | PF_W, 0x4000, 0x4000, 80, 80, 8);
    dynamic(0, DT_RELA, 0x800);
    dynamic(1, DT_RELASZ, 48);
    dynamic(2, DT_RELAENT, 24);
    dynamic(3, DT_RELACOUNT, 2);
    dynamic(4, DT_NULL, 0);
    rela(0, 0x4800, 41, 0x2000);
    rela(1, 0x4809, 41, ~Address(7)); // Unaligned pointer and negative addend.
    for (unsigned i = 0x4800; i < 0x4811; ++i)
      bytes[i] = 0xa5;
  }
  void dynamic(unsigned i, Address tag, Address value) {
    put(0x4000 + 16 * i, 8, tag);
    put(0x4008 + 16 * i, 8, value);
  }
  void rela(unsigned i, Address offset, Address info, Address addend) {
    put(0x800 + 24 * i, 8, offset);
    put(0x808 + 24 * i, 8, info);
    put(0x810 + 24 * i, 8, addend);
  }
  ElfError open() {
    return ElfImage::open_mapped({&mapping, 1}, 0, 64, ImageRole::Interpreter,
                                 bias, page, image);
  }
  Address get(unsigned at) const {
    Address value = 0;
    for (unsigned i = 0; i < 8; ++i)
      value = (value << 8) | bytes[at + i];
    return value;
  }
  RelocationError relocate() { return relocate_self(image, {&writable, 1}); }
};

extern "C" int main() {
  SelfFixture success;
  CHECK(success.open() == ElfError::None);
  CHECK(success.relocate() == RelocationError::None);
  CHECK(success.get(0x4800) == bias + 0x2000);
  CHECK(success.get(0x4809) == bias - 8);
  CHECK(success.bytes[0x4808] == 0xa5);
  CHECK(success.get(0x810) == 0x2000); // RELA addend remains unchanged.

  struct Mutation {
    unsigned offset;
    Address value;
    RelocationError error;
  };
  const Mutation bad[] = {
      {0x820, 42, RelocationError::Unsupported},
      {0x820, 5, RelocationError::Unsupported},
      {0x820, (Address(1) << 32) | 41, RelocationError::Unsupported},
      {0x820, 0, RelocationError::Unsupported},
      {0x828, 0x8000000000000000, RelocationError::Value},
      {0x828, ~bias, RelocationError::Value},
      {0x828, (Address(1) << 43) - bias, RelocationError::Value},
      {0x828, 0x7fffffffffffffff, RelocationError::Value},
      {0x818, 0x4800, RelocationError::Overlap},
      {0x818, 0x4804, RelocationError::Overlap},
      {0x818, 0x4000, RelocationError::Overlap},
      {0x818, 0x2000, RelocationError::Destination},
      {0x818, ~Address(0), RelocationError::Destination},
      {0x818, 0x5ffc, RelocationError::Destination},
      {0x4008, 0x5ff0, RelocationError::Table},
      {0x4018, 47, RelocationError::Table},
      {0x4018, ~Address(0), RelocationError::Table},
      {0x4028, 16, RelocationError::Table},
      {0x4038, 3, RelocationError::Table},
      {0x4020, DT_RELA, RelocationError::Dynamic},
      {0x4030, DT_INIT, RelocationError::Unsupported},
      {0x4030, DT_FINI_ARRAY, RelocationError::Unsupported},
      {0x4030, DT_PREINIT_ARRAY, RelocationError::Unsupported},
      {0x4030, DT_REL, RelocationError::Unsupported},
      {0x4030, DT_JMPREL, RelocationError::Unsupported},
      {0x4030, DT_TEXTREL, RelocationError::Unsupported},
      {0x4030, DT_SYMBOLIC, RelocationError::Unsupported},
      {0x4030, DT_AUDIT, RelocationError::Unsupported},
      {0x4030, 0x70000001, RelocationError::Unsupported},
      {0x4030, DT_FLAGS, RelocationError::Unsupported}, // Value 2 is SYMBOLIC.
      {0x4030, DT_FLAGS_1, RelocationError::Unsupported},
  };
  for (const auto &m : bad) {
    SelfFixture input;
    input.put(m.offset, 8, m.value);
    CHECK(input.open() == ElfError::None);
    RelocationError result = input.relocate();
    if (result != m.error)
      fprintf(stderr, "at %x expected %u got %u\n", m.offset, unsigned(m.error),
              unsigned(result));
    CHECK(result == m.error);
    CHECK(input.get(0x4800) == 0xa5a5a5a5a5a5a5a5); // No partial first write.
  }
  SelfFixture tls;
  tls.put(ph(4), 4, PT_TLS);
  CHECK(tls.open() == ElfError::None);
  CHECK(tls.relocate() == RelocationError::Unsupported);
  SelfFixture dependency;
  dependency.dynamic(3, DT_NEEDED, 0);
  CHECK(dependency.open() == ElfError::Dynamic);
  SelfFixture missing;
  missing.dynamic(2, DT_NULL, 0);
  CHECK(missing.open() == ElfError::None);
  CHECK(missing.relocate() == RelocationError::Table);
  SelfFixture empty;
  empty.dynamic(0, DT_NULL, 0);
  CHECK(empty.open() == ElfError::None);
  CHECK(empty.relocate() == RelocationError::None);
  SelfFixture truncated;
  truncated.mapping.bytes = Bytes(truncated.bytes, 0x4010);
  CHECK(truncated.open() == ElfError::Dynamic);
  SelfFixture absent_write;
  CHECK(absent_write.open() == ElfError::None);
  CHECK(relocate_self(absent_write.image, {}) == RelocationError::Destination);
  unsigned char unrelated[0x2000] = {};
  absent_write.writable.bytes = {unrelated, sizeof(unrelated)};
  CHECK(absent_write.relocate() == RelocationError::Destination);
  SelfFixture metadata;
  // Make header/RELA storage writable to exercise protected-input checks.
  metadata.put(ph(1, 4), 4, PF_R | PF_W);
  metadata.writable = {0, {metadata.bytes, sizeof(metadata.bytes)}};
  metadata.rela(1, 0, 41, 0);
  CHECK(metadata.open() == ElfError::None);
  CHECK(metadata.relocate() == RelocationError::Overlap);
  metadata.rela(1, 64, 41, 0);
  CHECK(metadata.relocate() == RelocationError::Overlap);
  metadata.rela(1, 0x800, 41, 0);
  CHECK(metadata.relocate() == RelocationError::Overlap);
  CHECK(metadata.get(0x4800) == 0xa5a5a5a5a5a5a5a5);
  return 0;
}
