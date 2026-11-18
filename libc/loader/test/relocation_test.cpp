//===-- Eager startup relocation tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/relocation.h"
#include "loader/linux/target.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

using namespace loader_test;
namespace cpp = LIBC_NAMESPACE::cpp;
static bool use_scratch;

static void check(bool condition, int line = __builtin_LINE()) {
  if (!condition) {
    fprintf(stderr, "relocation check failed at line %d\n", line);
    abort();
  }
}

static Address octa(const unsigned char *p) {
  Address value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value = (value << 8) | p[i];
  return value;
}

struct SnapshotStorage {
  alignas(8) unsigned char bytes[4096];
  size_t used = 0;
  bool fail = false;

  long allocate(size_t size, size_t alignment, void *&out) {
    out = nullptr;
    if (fail || alignment != 8 || used > sizeof(bytes) ||
        size > sizeof(bytes) - used)
      return -ENOMEM;
    out = bytes + used;
    used += size;
    return 0;
  }
};

struct Scope {
  DynamicFixture files[2];
  ElfImage images[2];
  DynamicInfo metadata[2];
  MappedRegion regions[2];
  WritableRegion storage[2];
  cpp::span<const WritableRegion> writable[2];
  SymbolObject objects[2];
  SnapshotStorage snapshots;

  long admit(bool owned) {
    for (unsigned i = 0; i < 2; ++i) {
      regions[i] = {0, files[i].view()};
      storage[i] = {0x4000, {files[i].bytes + 0x4000, 0x2000}};
      writable[i] = {&storage[i], 1};
      check(ElfImage::open_mapped({&regions[i], 1}, 0, 64,
                                  ImageRole::SharedObject, bias + i * 0x100000,
                                  page, images[i]) == ElfError::None);
      if (owned) {
        long error =
            DynamicInfo::open_stable(images[i], snapshots, metadata[i]);
        if (error)
          return error;
        check(metadata[i].is_stable_for(images[i]));
      } else {
        check(DynamicInfo::open(images[i], metadata[i]) == DynamicError::None);
      }
      objects[i] = {&images[i], &metadata[i]};
    }
    return 0;
  }
  void open() { check(admit(false) == 0); }
  StartupRelocationResult apply() {
    RelocationWrite scratch[4];
    return relocate_startup(objects, writable,
                            use_scratch ? cpp::span<RelocationWrite>(scratch)
                                        : cpp::span<RelocationWrite>());
  }
  void unchanged() {
    for (auto &f : files) {
      check(octa(f.bytes + 0x4800) == 0);
      check(octa(f.bytes + 0x4808) == 0);
    }
  }
};

static void test_relocations() {
  constexpr Address symbolic = (Address(1) << 32) | 42;
  Address value;
  check(runtime_relocation_value(41, 0x400000, 0x2000, value) &&
        value == 0x402000);
  check(runtime_relocation_value(symbolic, 0x700080, Address(-8), value) &&
        value == 0x700078);
  check(runtime_relocation_value(symbolic, 0, 0, value) && value == 0);
  check(!runtime_relocation_value(41, 0, 0, value));
  check(!runtime_relocation_value(symbolic, 0, Address(-1), value));
  check(!runtime_relocation_value(symbolic, ~Address(0), 1, value));
  check(!runtime_relocation_value(symbolic, 0, Address(1) << 63, value));
  check(runtime_relocation_value(symbolic, (Address(1) << 63) + page,
                                 Address(1) << 63, value) &&
        value == page);
  check(runtime_relocation_value(symbolic, page, 0, value));
  check(!runtime_relocation_value(symbolic, page, Address(-1), value));
  check(runtime_relocation_value(symbolic, (Address(1) << 43) - 1, 0, value));
  check(!runtime_relocation_value(symbolic, (Address(1) << 43) - 1, 1, value));

  Scope scope;
  scope.open();
  check(scope.apply().error == StartupRelocationError::None);
  check(octa(scope.files[0].bytes + 0x4800) == bias + 0x2000);
  check(octa(scope.files[1].bytes + 0x4800) == bias + 0x102000);
  check(octa(scope.files[1].bytes + 0x4808) == bias + 0x2000);
  // Both ordinary pointers and GOT entries use S+A exactly once.
  const unsigned pointer_types[] = {5, 42};
  for (unsigned type : pointer_types) {
    Scope pointers;
    pointers.files[1].put(0xb20, 8, (Address(1) << 32) | type);
    pointers.files[1].put(0xb28, 8, Address(-8));
    pointers.open();
    check(pointers.apply().error == StartupRelocationError::None);
    check(octa(pointers.files[1].bytes + 0x4808) == bias + 0x1ff8);
  }
  Scope protected_symbol;
  protected_symbol.files[1].bytes[0x81d] = STV_PROTECTED;
  protected_symbol.open();
  check(protected_symbol.apply().error == StartupRelocationError::None);
  check(octa(protected_symbol.files[1].bytes + 0x4808) == bias + 0x102000);
  Scope weak;
  for (auto &f : weak.files) {
    f.bytes[0x81c] = (STB_WEAK << 4) | STT_FUNC;
    f.put(0x81e, 2, SHN_UNDEF);
    f.put(0x820, 8, 0);
  }
  weak.open();
  check(weak.apply().error == StartupRelocationError::None);
  check(octa(weak.files[1].bytes + 0x4808) == 0);
  Scope missing;
  for (auto &f : missing.files) {
    f.put(0x81e, 2, SHN_UNDEF);
    f.put(0x820, 8, 0);
  }
  missing.open();
  check(missing.apply().error == StartupRelocationError::Symbol);
  missing.unchanged();
  Scope late_missing;
  late_missing.files[0].bytes[0x81d] = STV_HIDDEN;
  late_missing.files[1].put(0x81e, 2, SHN_UNDEF);
  late_missing.files[1].put(0x820, 8, 0);
  late_missing.open();
  auto failure = late_missing.apply();
  check(failure.error == StartupRelocationError::Symbol && failure.object == 1);
  late_missing.unchanged();
  Scope unordered;
  for (auto &f : unordered.files) {
    f.set(DT_RELACOUNT, 0);
    f.put(0xb08, 8, symbolic);
    f.put(0xb10, 8, 0);
    f.put(0xb20, 8, 41);
    f.put(0xb28, 8, 0x2000);
  }
  unordered.open();
  check(unordered.apply().error == StartupRelocationError::None);
  check(octa(unordered.files[1].bytes + 0x4800) == bias + 0x2000);
  check(octa(unordered.files[1].bytes + 0x4808) == bias + 0x102000);

  // Failure in the last object must leave even earlier RELATIVE words
  // untouched.
  Scope overflow;
  overflow.files[1].put(0xb28, 8, Address(1) << 43);
  overflow.open();
  check(overflow.apply().error == StartupRelocationError::Value);
  overflow.unchanged();
  Scope unaligned;
  unaligned.files[1].put(0xb18, 8, 0x4811);
  unaligned.open();
  check(unaligned.apply().error == StartupRelocationError::Destination);
  unaligned.unchanged();
  Scope partial_storage;
  partial_storage.open();
  partial_storage.storage[1] =
      {0x3ff8, {partial_storage.files[1].bytes + 0x3ff8, 0x1008}};
  check(partial_storage.apply().error == StartupRelocationError::None);
  check(octa(partial_storage.files[1].bytes + 0x4808) == bias + 0x2000);

  Scope wrong_storage;
  wrong_storage.open();
  wrong_storage.storage[1].bytes = wrong_storage.storage[0].bytes;
  check(wrong_storage.apply().error == StartupRelocationError::Destination);
  wrong_storage.unchanged();

  // Deliberately mutate test input after admission to exercise final preflight.
  const Address unsupported[] = {10, 43, 44, 45, 46, 47, 48, 49};
  for (Address type : unsupported) {
    Scope bad;
    bad.open();
    bad.files[1].put(0xb20, 8, (Address(1) << 32) | type);
    check(bad.apply().error == StartupRelocationError::Metadata);
    bad.unchanged();
  }
  for (unsigned test = 0; test < 6; ++test) {
    Scope bad;
    bad.open();
    auto &f = bad.files[1];
    if (test == 0)
      f.set(DT_RELACOUNT, 2);
    if (test == 1)
      f.put(0xb20, 8, (Address(9) << 32) | 42);
    if (test == 2)
      f.put(0xb18, 8, 0x4800);
    if (test == 3)
      f.put(0xb18, 8, 0x2000);
    if (test == 4)
      f.put(0xb18, 8, 0x4000);
    if (test == 5)
      f.put(0xb08, 8, (Address(1) << 32) | 41);
    check(bad.apply().error == StartupRelocationError::Metadata);
    bad.unchanged();
  }
  puts("startup relocation: arithmetic, binding, writes and whole-scope "
       "preflight passed");
}

static void test_snapshot_relocations() {
  Scope stable;
  check(stable.admit(true) == 0);
  stable.files[1].put(0xb18, 8, 0x4810);
  stable.files[1].put(0xb20, 8, (Address(1) << 32) | 10);
  check(stable.apply().error == StartupRelocationError::None);
  check(octa(stable.files[1].bytes + 0x4808) == bias + 0x2000);
  check(octa(stable.files[1].bytes + 0x4810) == 0);

  Scope malformed;
  malformed.files[1].put(0xb20, 8, (Address(1) << 32) | 10);
  check(malformed.admit(true) == -ENOEXEC);
  malformed.unchanged();

  Scope allocation;
  allocation.snapshots.fail = true;
  check(allocation.admit(true) == -ENOMEM);
  check(!allocation.metadata[0].is_valid());
  allocation.unchanged();

  Scope mismatched;
  check(mismatched.admit(true) == 0);
  mismatched.objects[1].metadata = &mismatched.metadata[0];
  auto error = mismatched.apply();
  check(error.error == StartupRelocationError::Metadata && error.object == 1);
  mismatched.unchanged();

  Scope wrong_storage;
  check(wrong_storage.admit(true) == 0);
  wrong_storage.storage[1].bytes = wrong_storage.storage[0].bytes;
  error = wrong_storage.apply();
  check(error.error == StartupRelocationError::Destination &&
        error.object == 1);
  wrong_storage.unchanged();
}

extern "C" int main() {
  test_relocations();
  test_snapshot_relocations();
  use_scratch = true;
  test_relocations();
  test_snapshot_relocations();
  Scope short_scratch;
  short_scratch.open();
  RelocationWrite scratch[3];
  check(relocate_startup(short_scratch.objects, short_scratch.writable, scratch)
            .error == StartupRelocationError::Image);
  short_scratch.unchanged();
  Scope large_scratch;
  large_scratch.open();
  RelocationWrite extra[6]{};
  check(relocate_startup(large_scratch.objects, large_scratch.writable, extra)
            .error == StartupRelocationError::None);
  check(octa(large_scratch.files[1].bytes + 0x4808) == bias + 0x2000);
  // Native validation may borrow unaligned host storage for aligned target
  // addresses. Prepared writes must not impose an extra host-alignment rule.
  DynamicFixture input;
  alignas(8) unsigned char bytes[sizeof(input.bytes) + 1];
  for (size_t i = 0; i < sizeof(input.bytes); ++i)
    bytes[i + 1] = input.bytes[i];
  MappedRegion region{0, {bytes + 1, sizeof(input.bytes)}};
  ElfImage image;
  DynamicInfo info;
  check(ElfImage::open_mapped({&region, 1}, 0, 64, ImageRole::SharedObject,
                            bias, page, image) == ElfError::None);
  check(DynamicInfo::open(image, info) == DynamicError::None);
  SymbolObject object{&image, &info};
  WritableRegion write{0x4000, {bytes + 0x4001, 0x2000}};
  cpp::span<const WritableRegion> writes{&write, 1};
  check(relocate_startup({&object, 1}, {&writes, 1}, scratch).error ==
        StartupRelocationError::None);
  check(octa(bytes + 0x4801) == bias + 0x2000);
  check(octa(bytes + 0x4809) == bias + 0x2000);
  return 0;
}
