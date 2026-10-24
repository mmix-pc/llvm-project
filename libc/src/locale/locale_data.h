//===-- Private locale data storage -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_LOCALE_DATA_H
#define LLVM_LIBC_SRC_LOCALE_LOCALE_DATA_H

#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/CPP/span.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// A byte-oriented container, not a public locale_t or a native struct dump.
// Semantic category validation must precede publication as a usable locale.
// Non-owning views require their input to remain alive and immutable.
class LocaleDataView {
public:
  static constexpr size_t HEADER_SIZE = 32;
  static constexpr size_t DIRECTORY_SIZE = 6 * 16;
  static constexpr size_t ENTRY_SIZE = 16;
  static constexpr size_t MAX_SIZE = 64 * 1024 * 1024;
  enum class Kind : uint32_t {
    STRING = 1,
    WORDS = 2,
    BYTES = 3,
    REFERENCE = 4
  };

  struct Item {
    Kind kind;
    cpp::span<const uint8_t> bytes;
    // WORDS are stored big-endian and may be unaligned.
    ErrorOr<uint32_t> word(size_t index) const;
  };

  static ErrorOr<LocaleDataView> parse(cpp::span<const uint8_t> bytes);
  ErrorOr<Item> find(unsigned category, uint32_t key) const;
  const char *name() const;

private:
  cpp::span<const uint8_t> bytes;
  explicit LocaleDataView(cpp::span<const uint8_t> bytes) : bytes(bytes) {}
  ErrorOr<Item> find_raw(unsigned category, uint32_t key) const;
};

// Own a frozen copy rather than retaining a mutable file mapping or input span.
// A reference must already be held while retaining another one.
class LocaleDataFile {
public:
  static ErrorOr<LocaleDataFile *> copy(cpp::span<const uint8_t> bytes);
  LocaleDataFile(const LocaleDataFile &) = delete;
  LocaleDataFile &operator=(const LocaleDataFile &) = delete;
  const LocaleDataView &view() const { return data; }
  bool retain();
  void release();

private:
  cpp::Atomic<unsigned> references{1};
  uint8_t *storage;
  LocaleDataView data;
  LocaleDataFile(uint8_t *storage, LocaleDataView data)
      : storage(storage), data(data) {}
  ~LocaleDataFile();
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_LOCALE_DATA_H
