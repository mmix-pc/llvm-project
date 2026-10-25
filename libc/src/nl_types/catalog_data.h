//===-- Owned numbered message catalogs -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NL_TYPES_CATALOG_DATA_H
#define LLVM_LIBC_SRC_NL_TYPES_CATALOG_DATA_H

#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/span.h"
#include "src/__support/error_or.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// GNU gencat's dual-index container. Message bytes have no embedded charset
// metadata. The opener selects by locale/name; message bytes are returned
// unchanged, without inventing a source encoding or transcoding them.
class MessageCatalog {
public:
  static constexpr size_t MAX_SIZE = 64 * 1024 * 1024;
  static ErrorOr<MessageCatalog *> copy(cpp::span<const uint8_t> bytes);
  MessageCatalog(const MessageCatalog &) = delete;
  MessageCatalog &operator=(const MessageCatalog &) = delete;
  // Missing keys return nullptr; public catgets owns errno/default-message policy.
  const char *lookup(int set, int message) const;
  void destroy();

private:
  struct Record {
    uint32_t set, message, offset;
  };
  uint8_t *bytes;
  Record *index;
  size_t count;
  const char *strings;
  MessageCatalog(uint8_t *bytes, Record *index, size_t count,
                 const char *strings)
      : bytes(bytes), index(index), count(count), strings(strings) {}
  ~MessageCatalog();
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_SRC_NL_TYPES_CATALOG_DATA_H
