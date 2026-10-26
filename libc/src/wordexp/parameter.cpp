//===-- Parameter expansion operators -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/parameter.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<ParameterExpansion> parse_word_parameter(cpp::string_view contents) {
  using Op = ParameterExpansion::Operation;
  size_t at = 0;
  bool length = !contents.empty() && contents[0] == '#';
  if (length)
    ++at;
  auto first = [](char c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  };
  if (at == contents.size() || !first(contents[at]))
    return Error(EINVAL);
  size_t start = at++;
  while (at < contents.size() &&
         (first(contents[at]) || (contents[at] >= '0' && contents[at] <= '9')))
    ++at;
  ParameterExpansion result{contents.substr(start, at - start),
                            {},
                            length ? Op::LENGTH : Op::VALUE,
                            false};
  if (at == contents.size())
    return result;
  if (length)
    return Error(EINVAL);
  result.test_null = contents[at] == ':';
  if (result.test_null && ++at == contents.size())
    return Error(EINVAL);
  char operation = contents[at++];
  switch (operation) {
  case '-':
    result.operation = Op::DEFAULT;
    break;
  case '=':
    result.operation = Op::ASSIGN;
    break;
  case '?':
    result.operation = Op::ERROR;
    break;
  case '+':
    result.operation = Op::ALTERNATIVE;
    break;
  case '#':
  case '%':
    if (result.test_null)
      return Error(EINVAL);
    result.operation = operation == '#' ? Op::PREFIX : Op::SUFFIX;
    if (at < contents.size() && contents[at] == operation) {
      ++at;
      result.operation =
          operation == '#' ? Op::LONGEST_PREFIX : Op::LONGEST_SUFFIX;
    }
    break;
  default:
    return Error(EINVAL);
  }
  result.word = contents.substr(at);
  return result;
}

ParameterExpansion::Action ParameterExpansion::select(bool is_set,
                                                      bool is_empty) const {
  bool missing = !is_set || (test_null && is_empty);
  switch (operation) {
  case Operation::VALUE:
    return Action::VALUE;
  case Operation::LENGTH:
    return Action::LENGTH;
  case Operation::DEFAULT:
    return missing ? Action::WORD : Action::VALUE;
  case Operation::ASSIGN:
    return missing ? Action::ASSIGN_WORD : Action::VALUE;
  case Operation::ERROR:
    return missing ? Action::ERROR_WORD : Action::VALUE;
  case Operation::ALTERNATIVE:
    return missing ? Action::EMPTY : Action::WORD;
  case Operation::PREFIX:
  case Operation::LONGEST_PREFIX:
  case Operation::SUFFIX:
  case Operation::LONGEST_SUFFIX:
    return Action::PATTERN;
  }
  __builtin_unreachable();
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
