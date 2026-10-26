//===-- Bounded message plural expressions ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LIBINTL_PLURAL_FORMS_H
#define LLVM_LIBC_SRC_LIBINTL_PLURAL_FORMS_H
#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/error_or.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// GNU MO Plural-Forms grammar, with unsigned-long arithmetic and lazy logical
// and conditional evaluation. Owns its parsed tree, not the metadata bytes.
class PluralForms {
public:
  static constexpr size_t MAX_LENGTH = 4096;
  static constexpr unsigned MAX_NODES = 256, MAX_DEPTH = 64;
  static ErrorOr<PluralForms> parse(cpp::string_view metadata);
  ErrorOr<unsigned long> select(unsigned long n) const;
  unsigned long size() const { return count; }

private:
  friend class PluralParser;
  enum class Op : uint8_t {
    NUMBER,
    N,
    NOT,
    MUL,
    DIV,
    MOD,
    ADD,
    SUB,
    LT,
    LE,
    GT,
    GE,
    EQ,
    NE,
    AND,
    OR,
    CONDITIONAL
  };
  struct Node {
    Op op = Op::NUMBER;
    uint8_t depth = 0;
    uint16_t left = 0, right = 0, third = 0;
    unsigned long value = 0;
  };
  Node nodes[MAX_NODES]{};
  unsigned used = 0, root = 0;
  unsigned long count = 0;
  ErrorOr<unsigned long> evaluate(unsigned index, unsigned long n) const;
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
