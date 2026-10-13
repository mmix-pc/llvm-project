//===-- Startup symbol binding tests --------------------------------------===//
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
static Bytes foo() {
  static constexpr unsigned char name[] = "foo";
  return {name, sizeof(name) - 1};
}
struct Symbols : DynamicFixture {
  void define(unsigned binding, unsigned visibility, unsigned section = 1,
              Address value = 0x2000, unsigned type = STT_FUNC) {
    bytes[0x81c] = (binding << 4) | type;
    bytes[0x81d] = visibility;
    put(0x81e, 2, section);
    put(0x820, 8, value);
  }
  void remove(Address tag) {
    unsigned offset = at(tag);
    for (unsigned i = offset; i + 16 < 0x4000 + entries * 16; ++i)
      bytes[i] = bytes[i + 16];
    --entries;
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
  // Explicit scopes are the parser-level seam; graph order is tested
  // separately.
  Symbols files[3];
  ElfImage images[3];
  DynamicInfo metadata[3];
  SymbolObject objects[] = {{&images[0], &metadata[0]},
                            {&images[1], &metadata[1]},
                            {&images[2], &metadata[2]}};
  StartupSymbols scope;
  auto reopen = [&]() {
    for (unsigned i = 0; i < 3; ++i) {
      if (ElfImage::open_file(files[i].view(), ImageRole::SharedObject,
                              bias + i * 0x100000, page,
                              images[i]) != ElfError::None ||
          DynamicInfo::open(images[i], metadata[i]) != DynamicError::None)
        return false;
    }
    return StartupSymbols::open(objects, scope) == SymbolError::None;
  };
  SymbolValue value;
  SymbolFailure failure;
  CHECK(scope.lookup(foo(), value) == SymbolError::Scope);
  CHECK(reopen());
  CHECK(scope.resolve(1, 1, value) == SymbolError::None);
  CHECK(value.object == 0 && value.address == bias + 0x2000 &&
        !value.weak_zero);
  SymbolValue function_pointer;
  CHECK(scope.lookup(foo(), function_pointer) == SymbolError::None);
  CHECK(function_pointer.address == value.address);
  CHECK(scope.validate_references(failure) == SymbolError::None);
  CHECK(scope.resolve(3, 1, value) == SymbolError::Index && !value.address);
  CHECK(scope.resolve(0, 0, value) == SymbolError::Index);
  CHECK(scope.resolve(0, 2, value) == SymbolError::Index);

  files[0].define(STB_WEAK, STV_DEFAULT);
  CHECK(reopen());
  CHECK(scope.resolve(2, 1, value) == SymbolError::None && value.object == 0);
  files[1].define(STB_GLOBAL, STV_PROTECTED);
  CHECK(reopen());
  CHECK(scope.resolve(1, 1, value) == SymbolError::None && value.object == 1);
  CHECK(value.address == bias + 0x100000 + 0x2000);
  CHECK(scope.resolve(2, 1, value) == SymbolError::None && value.object == 0);
  files[0].define(STB_GLOBAL, STV_DEFAULT, SHN_UNDEF, 0);
  CHECK(reopen());
  CHECK(scope.resolve(0, 1, value) == SymbolError::None && value.object == 1);
  CHECK(scope.lookup(foo(), function_pointer) == SymbolError::None);
  CHECK(value.address == function_pointer.address);

  const unsigned visibilities[] = {STV_HIDDEN, STV_INTERNAL};
  for (unsigned visibility : visibilities) {
    files[1].define(STB_GLOBAL, visibility);
    CHECK(reopen());
    CHECK(scope.resolve(0, 1, value) == SymbolError::None && value.object == 2);
    CHECK(scope.resolve(1, 1, value) == SymbolError::None && value.object == 1);
  }
  files[1].define(STB_LOCAL, STV_DEFAULT);
  CHECK(reopen());
  CHECK(scope.resolve(0, 1, value) == SymbolError::None && value.object == 2);
  CHECK(scope.resolve(1, 1, value) == SymbolError::None && value.object == 1);
  files[0].define(STB_GLOBAL, STV_HIDDEN, SHN_UNDEF, 0);
  CHECK(reopen());
  CHECK(scope.resolve(0, 1, value) == SymbolError::Undefined);

  for (auto &file : files)
    file.define(STB_WEAK, STV_DEFAULT, SHN_UNDEF, 0);
  CHECK(reopen());
  CHECK(scope.resolve(0, 1, value) == SymbolError::None && value.weak_zero &&
        !value.address);
  CHECK(scope.lookup(foo(), value) == SymbolError::Undefined &&
        !value.weak_zero);
  CHECK(scope.validate_references(failure) == SymbolError::None);
  files[0].define(STB_GLOBAL, STV_DEFAULT, SHN_UNDEF, 0);
  CHECK(reopen());
  CHECK(scope.validate_references(failure) == SymbolError::Undefined);
  CHECK(failure.object == 0 && failure.symbol == 1);
  // An unused dynsym declaration does not require a runtime definition.
  files[0].remove(DT_RELA);
  files[0].remove(DT_RELASZ);
  files[0].remove(DT_RELAENT);
  files[0].remove(DT_RELACOUNT);
  CHECK(reopen());
  CHECK(scope.validate_references(failure) == SymbolError::None);

  files[0].define(STB_GLOBAL, STV_DEFAULT, SHN_ABS, 0, STT_OBJECT);
  CHECK(reopen());
  CHECK(scope.resolve(1, 1, value) == SymbolError::None && !value.weak_zero &&
        !value.address);
  files[0].define(STB_GLOBAL, STV_DEFAULT, SHN_ABS, 0x12345, STT_OBJECT);
  CHECK(reopen());
  CHECK(scope.lookup(foo(), value) == SymbolError::None &&
        value.address == 0x12345);
  files[0].define(STB_GLOBAL, STV_DEFAULT, 1, 0x6000, STT_OBJECT);
  files[0].put(0x828, 8, 0);
  CHECK(reopen());
  CHECK(scope.lookup(foo(), value) == SymbolError::None &&
        value.address == bias + 0x6000);
  files[0].put(0x828, 8, 8);
  CHECK(!reopen()); // Declared extent exceeds the admitted LOAD.
  files[0] = Symbols();
  files[0].define(STB_GLOBAL, STV_DEFAULT, 1, 0x2001);
  CHECK(!reopen()); // Unaligned function address.
  files[0].define(STB_GLOBAL, STV_DEFAULT, 1, 0x4000);
  CHECK(!reopen()); // Function in non-executable data.

  for (unsigned mode = 0; mode < 3; ++mode) {
    files[0] = Symbols();
    if (mode == 1)
      files[0].remove(DT_GNU_HASH);
    if (mode == 2)
      files[0].remove(DT_HASH);
    CHECK(reopen());
    Address index;
    CHECK(metadata[0].find_definition(foo(), index) && index == 1);
    const unsigned char absent[] = "missing";
    CHECK(!metadata[0].find_definition({absent, sizeof(absent) - 1}, index) &&
          !index);
    CHECK(scope.lookup(foo(), value) == SymbolError::None && value.object == 0);
    // Continue past an ineligible same-name entry in either hash chain.
    for (unsigned i = 0; i < 24; ++i)
      files[0].bytes[0x830 + i] = files[0].bytes[0x818 + i];
    files[0].define(STB_GLOBAL, STV_HIDDEN);
    files[0].put(0x904, 4, 3);
    files[0].put(0x910, 4, 2);
    files[0].put(0x914, 4, 0);
    files[0].put(0xa1c, 4, 0x0b887388);
    files[0].put(0xa20, 4, 0x0b887389);
    CHECK(reopen());
    CHECK(metadata[0].find_definition(foo(), index) && index == 2);
  }
  CHECK(StartupSymbols::open({}, scope) == SymbolError::Scope);
  CHECK(scope.resolve(0, 1, value) == SymbolError::Scope);
  puts("loader startup symbol binding, visibility and reference checks passed");
  return 0;
}
