//===-- Translation domain bindings -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LIBINTL_DOMAIN_BINDINGS_H
#define LLVM_LIBC_SRC_LIBINTL_DOMAIN_BINDINGS_H

#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Returned strings are borrowed. Callers synchronize their use with mutations
// that may invalidate them; queries never allocate or change bindings.
ErrorOr<char *> current_text_domain(const char *name);
enum class DomainBinding { DIRECTORY, CODESET };
ErrorOr<char *> bind_text_domain(const char *name, const char *value,
                                 DomainBinding kind);

// Own a coherent selection across file I/O without holding the binding mutex
// or borrowing strings that a simultaneous binding update can replace.
class DomainSnapshot {
  char *storage = nullptr;
  const char *domain_value = nullptr, *directory_value = nullptr,
             *codeset_value = nullptr;
  friend ErrorOr<void> snapshot_text_domain(const char *, DomainSnapshot &);

public:
  DomainSnapshot() = default;
  DomainSnapshot(const DomainSnapshot &) = delete;
  DomainSnapshot &operator=(const DomainSnapshot &) = delete;
  DomainSnapshot(DomainSnapshot &&other)
      : storage(other.storage), domain_value(other.domain_value),
        directory_value(other.directory_value),
        codeset_value(other.codeset_value) {
    other.storage = nullptr;
  }
  ~DomainSnapshot();
  const char *domain() const { return domain_value; }
  const char *directory() const { return directory_value; }
  const char *codeset() const { return codeset_value; }
};
ErrorOr<void> snapshot_text_domain(const char *name, DomainSnapshot &result);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
