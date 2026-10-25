//===-- Immutable GNU MO message catalogs -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LIBINTL_MO_CATALOG_H
#define LLVM_LIBC_SRC_LIBINTL_MO_CATALOG_H

#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/CPP/span.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// GNU MO revision 0, in either byte order. Other revisions (including the
// system-dependent string extension) return ENOTSUP, malformed containers
// EINVAL. Views borrow storage that must remain alive and immutable.
class MoCatalogView {
public:
  static constexpr size_t HEADER_SIZE = 28;
  static constexpr size_t MAX_SIZE = 64 * 1024 * 1024;
  struct Message {
    // Full counted strings, excluding only their final terminator. The original
    // can contain msgid\0msgid_plural, translation can contain NUL-separated
    // forms. Neither charset conversion nor plural evaluation is performed.
    cpp::string_view original;
    cpp::string_view translation;
  };

  static ErrorOr<MoCatalogView> parse(cpp::span<const uint8_t> bytes);
  size_t size() const { return count; }
  ErrorOr<Message> at(size_t index) const;
  // key is the singular msgid, including any context and EOT separator. A NUL
  // in the supplied key is EINVAL; an absent key is ENOENT.
  ErrorOr<Message> find(cpp::string_view key) const;
  ErrorOr<cpp::string_view> metadata() const;
  // Raw metadata values, with surrounding horizontal whitespace removed.
  // Missing fields return ENOENT; ambiguous/malformed fields return EINVAL.
  ErrorOr<cpp::string_view> header_field(cpp::string_view name) const;
  ErrorOr<cpp::string_view> charset() const;
  ErrorOr<cpp::string_view> plural_forms() const;

private:
  cpp::span<const uint8_t> bytes;
  size_t count, originals, translations;
  bool big;
  MoCatalogView(cpp::span<const uint8_t> bytes, size_t count, size_t originals,
                size_t translations, bool big)
      : bytes(bytes), count(count), originals(originals),
        translations(translations), big(big) {}
  cpp::string_view string(size_t table, size_t index) const;
};

// Frozen owned bytes, independent of the input after copy(). Each borrowed view
// or result requires a live reference. Retaining requires an existing reference;
// saturation returns false without changing the count.
class MoCatalog {
public:
  static ErrorOr<MoCatalog *> copy(cpp::span<const uint8_t> bytes);
  MoCatalog(const MoCatalog &) = delete;
  MoCatalog &operator=(const MoCatalog &) = delete;
  const MoCatalogView &view() const { return data; }
  bool retain();
  void release();

private:
  cpp::Atomic<unsigned> references{1};
  uint8_t *storage;
  MoCatalogView data;
  MoCatalog(uint8_t *storage, MoCatalogView data)
      : storage(storage), data(data) {}
  ~MoCatalog();
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LIBINTL_MO_CATALOG_H
