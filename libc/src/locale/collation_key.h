//===-- Collation comparison and transform keys -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_COLLATION_KEY_H
#define LLVM_LIBC_SRC_LOCALE_COLLATION_KEY_H

#include "hdr/types/wchar_t.h"
#include "src/locale/collation_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// The caller owns the decoded scalar and element buffers and retains the data
// file. Only elements from the same validated category may be compared.
ErrorOr<size_t> resolve_collation(const CollationData &data,
                                  cpp::span<const uint32_t> source,
                                  cpp::span<CollationData::Rule> elements);
int compare_collation(cpp::span<const CollationData::Rule> left,
                      cpp::span<const CollationData::Rule> right);

// Produce an order-preserving, NUL-free byte key; return the complete length
// excluding NUL even when the destination is short. This does not allocate.
ErrorOr<size_t>
transform_collation(cpp::span<const CollationData::Rule> elements,
                    cpp::span<uint8_t> destination);

// The same ordered tokens represented as positive wide elements. Capacity and
// the returned complete length count wchar_t elements, never storage bytes.
ErrorOr<size_t>
transform_collation(cpp::span<const CollationData::Rule> elements,
                    cpp::span<wchar_t> destination);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_COLLATION_KEY_H
