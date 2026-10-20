//===-- GNU symbol version validation and binding tests
//--------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/symbols.h"
#include <stdio.h>

using namespace loader_test;
static Bytes text(const char *s) {
  size_t n = 0;
  while (s[n])
    ++n;
  return {reinterpret_cast<const unsigned char *>(s), n};
}
struct VersionFixture : DynamicFixture {
  explicit VersionFixture(bool provider) {
    // Use a SysV chain containing two equal spellings in the provider.
    put(at(DT_GNU_HASH), 8, DT_VERSYM);
    set(DT_VERSYM, 0xd00);
    bytes[0x740] = 'V';
    bytes[0x741] = '1';
    bytes[0x744] = 'V';
    bytes[0x745] = '2';
    set(DT_STRSZ, 0x48);
    --entries;
    if (provider) {
      put(0x904, 4, 3);
      put(0x910, 4, 2); // chain[1] -> symbol 2.
      put(0x830, 4, 1);
      bytes[0x834] = (STB_GLOBAL << 4) | STT_FUNC;
      put(0x836, 2, 1);
      put(0x838, 8, 0x2004);
      put(0x840, 8, 4);
      put(0xd02, 2, 0x8002);
      put(0xd04, 2, 3);
      for (unsigned i = 0; i < 2; ++i) {
        unsigned at = 0xc00 + i * 28;
        put(at, 2, 1);
        put(at + 4, 2, 2 + i);
        put(at + 6, 2, 1);
        put(at + 8, 4, 0x591 + i); // SysV hash of V1/V2.
        put(at + 12, 4, 20);
        put(at + 16, 4, i ? 0 : 28);
        put(at + 20, 4, 0x40 + i * 4);
      }
      add(DT_VERDEF, 0xc00);
      add(DT_VERDEFNUM, 2);
    } else {
      put(0x81e, 2, SHN_UNDEF);
      put(0x820, 8, 0);
      put(0xd02, 2, 7); // A consumer-local index need not match its provider.
      put(0xe00, 2, 1);
      put(0xe02, 2, 1);
      put(0xe04, 4, 5); // libx.so from DT_NEEDED.
      put(0xe08, 4, 16);
      put(0xe10, 4, 0x591);
      put(0xe16, 2, 7);
      put(0xe18, 4, 0x40);
      add(DT_VERNEED, 0xe00);
      add(DT_VERNEEDNUM, 1);
    }
    add(DT_NULL, 0);
    put(ph(5, 32), 8, entries * 16);
    put(ph(5, 40), 8, entries * 16);
  }
};

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                  \
      return 1;                                                                \
    }                                                                          \
  } while (false)

extern "C" int main() {
  VersionFixture provider(true), consumer(false);
  DynamicFixture plain;
  ElfImage images[3];
  DynamicInfo infos[3];
  auto open = [&](Fixture &f, unsigned i) {
    if (ElfImage::open_file(f.view(), ImageRole::SharedObject,
                            bias + i * 0x100000, page,
                            images[i]) != ElfError::None)
      return DynamicError::Image;
    return DynamicInfo::open(images[i], infos[i]);
  };
  CHECK(open(provider, 0) == DynamicError::None);
  CHECK(open(consumer, 1) == DynamicError::None);
  CHECK(open(plain, 2) == DynamicError::None);
  Address index;
  CHECK(infos[0].find_definition(text("foo"), index) && index == 2);
  CHECK(infos[0].find_definition(text("foo"), index, text("V1")) && index == 1);
  CHECK(infos[0].find_definition(text("foo"), index, text("V2")) && index == 2);
  CHECK(!infos[0].find_definition(text("foo"), index, text("V3")));
  CHECK(!infos[2].find_definition(text("foo"), index, text("V1")));
  CHECK(infos[1].accepts_dependency(text("libx.so"), infos[0]));
  CHECK(!infos[1].accepts_dependency(text("libx.so"), infos[2]));
  SymbolObject objects[] = {{&images[2], &infos[2]},
                            {&images[0], &infos[0]},
                            {&images[1], &infos[1]}};
  StartupSymbols scope;
  SymbolValue value;
  CHECK(StartupSymbols::open(objects, scope) == SymbolError::None);
  CHECK(scope.resolve(2, 1, value) == SymbolError::None);
  CHECK(value.object == 1 && value.symbol == 1);
  CHECK(scope.lookup(text("foo"), value) == SymbolError::None &&
        value.object == 0);

  // Weak dependency flags do not make a strong unresolved reference weak.
  consumer.put(0xe14, 2, VER_FLG_WEAK);
  CHECK(open(consumer, 1) == DynamicError::None);
  CHECK(infos[1].accepts_dependency(text("libx.so"), infos[2]));
  SymbolObject missing[] = {{&images[1], &infos[1]}, {&images[2], &infos[2]}};
  CHECK(StartupSymbols::open(missing, scope) == SymbolError::None);
  CHECK(scope.resolve(0, 1, value) == SymbolError::Undefined);
  consumer.bytes[0x81c] = (STB_WEAK << 4) | STT_FUNC;
  CHECK(open(consumer, 1) == DynamicError::None);
  CHECK(scope.resolve(0, 1, value) == SymbolError::None && value.weak_zero);
  consumer.bytes[0x81c] = (STB_GLOBAL << 4) | STT_FUNC;
  consumer.put(0xd02, 2, 0);
  CHECK(open(consumer, 1) == DynamicError::None);
  CHECK(scope.resolve(0, 1, value) == SymbolError::None && value.object == 1 &&
        !value.weak_zero);

  // Localized definitions bind internally, not to the public default.
  provider.put(0xd02, 2, 0);
  CHECK(open(provider, 0) == DynamicError::None);
  CHECK(StartupSymbols::open(objects, scope) == SymbolError::None);
  CHECK(scope.resolve(1, 1, value) == SymbolError::None && value.object == 1 &&
        value.symbol == 1);
  CHECK(!infos[0].find_definition(text("foo"), index, text("V1")));

  const unsigned offsets[] = {0xc00, 0xc04, 0xc06, 0xc08, 0xc0c,
                              0xc10, 0xc14, 0xc18, 0xc20, 0xd02};
  const unsigned widths[] = {2, 2, 2, 4, 4, 4, 4, 4, 2, 2};
  const Address invalid[] = {2, 0, 0, 0, 0xfffffff0, 4, 0x1000, 8, 2, 9};
  for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
    VersionFixture malformed(true);
    malformed.put(offsets[i], widths[i], invalid[i]);
    CHECK(open(malformed, 0) == DynamicError::Version);
    CHECK(!infos[0].is_valid());
  }
  const unsigned need_offsets[] = {0xe00, 0xe02, 0xe04, 0xe08, 0xe0c,
                                   0xe10, 0xe14, 0xe16, 0xe18, 0xe1c};
  const unsigned need_widths[] = {2, 2, 4, 4, 4, 4, 2, 2, 4, 4};
  const Address need_invalid[] = {2, 0, 13, 8, 16, 0, 1, 1, 0x1000, 16};
  for (unsigned i = 0; i < sizeof(need_offsets) / sizeof(need_offsets[0]);
       ++i) {
    VersionFixture malformed(false);
    malformed.put(need_offsets[i], need_widths[i], need_invalid[i]);
    CHECK(open(malformed, 1) == DynamicError::Version);
  }
  VersionFixture truncated(true);
  truncated.set(DT_VERDEF, 0xffc);
  CHECK(open(truncated, 0) == DynamicError::Version);
  VersionFixture oversized(true);
  oversized.set(DT_VERDEFNUM, 32768);
  CHECK(open(oversized, 0) == DynamicError::Version);
  VersionFixture orphan(true);
  unsigned count_tag = orphan.at(DT_VERDEFNUM);
  orphan.put(count_tag, 8, DT_NEEDED);
  orphan.put(count_tag + 8, 8, 5);
  CHECK(open(orphan, 0) == DynamicError::Version);
  VersionFixture overlap(true);
  for (unsigned i = 0; i < 56; ++i)
    overlap.bytes[0x4a00 + i] = overlap.bytes[0xc00 + i];
  overlap.set(DT_VERDEF, 0x4a00);
  overlap.put(0xb00, 8, 0x4a00);
  CHECK(open(overlap, 0) == DynamicError::Relocation);
  VersionFixture duplicate(true);
  duplicate.put(0xc24, 4, 0x591);
  duplicate.put(0xc30, 4, 0x40);
  CHECK(open(duplicate, 0) == DynamicError::Version);
  VersionFixture ambiguous(true);
  ambiguous.put(0xd02, 2, 2);
  CHECK(open(ambiguous, 0) == DynamicError::Version);
  VersionFixture repeated(true);
  repeated.put(0xd04, 2, 0x8002);
  CHECK(open(repeated, 0) == DynamicError::Version);
  VersionFixture parent(true);
  parent.put(0xc22, 2, 2); // V2 has a second Verdaux naming V1.
  parent.put(0xc34, 4, 8);
  parent.put(0xc38, 4, 0x40);
  CHECK(open(parent, 0) == DynamicError::None);
  parent.put(0xc38, 4, 0x44); // A node cannot inherit itself.
  CHECK(open(parent, 0) == DynamicError::Version);
  VersionFixture hidden_need(false);
  hidden_need.put(0xe16, 2, 0x8007);
  CHECK(open(hidden_need, 1) == DynamicError::None);
  Bytes version;
  CHECK(infos[1].symbol_version(1, version));
  CHECK(version.size() == 2 && version[0] == 'V' && version[1] == '1');
  return 0;
}
