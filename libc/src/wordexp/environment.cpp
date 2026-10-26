//===-- Word expansion environment ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/environment.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/realloc.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/integer_to_string.h"
#include "src/__support/str_to_integer.h"
#include "src/pwd/getpwnam_r.h"
#include "src/stdlib/environ_internal.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

WordEnvironment::~WordEnvironment() { ::free(account_buffer); }

ErrorOr<WordVariable> WordEnvironment::lookup(void *, cpp::string_view name) {
  const char *value = EnvironmentManager::get_instance().get(name);
  return WordVariable{value != nullptr,
                      value ? cpp::string_view(value) : cpp::string_view{}};
}

int WordEnvironment::assign(void *, cpp::string_view name,
                            cpp::string_view value) {
  return EnvironmentManager::get_instance().set(name, value, true) ? ENOMEM : 0;
}

ErrorOr<WordVariable> WordEnvironment::home(void *context,
                                            cpp::string_view user) {
  if (user.empty())
    return lookup(context, "HOME");
  CharVector name;
  for (char c : user)
    if (!name.append(c))
      return Error(ENOMEM);
  auto &self = *static_cast<WordEnvironment *>(context);
  if (!self.account_capacity) {
    self.account_buffer = static_cast<char *>(::realloc(nullptr, 1024));
    if (!self.account_buffer)
      return Error(ENOMEM);
    self.account_capacity = 1024;
  }
  for (;;) {
    struct passwd entry, *found = nullptr;
    int error =
        LIBC_NAMESPACE::getpwnam_r(name.c_str(), &entry, self.account_buffer,
                                   self.account_capacity, &found);
    if (error != ERANGE) {
      if (error)
        return Error(error);
      return found ? WordVariable{true, entry.pw_dir} : WordVariable{false, {}};
    }
    if (self.account_capacity > cpp::numeric_limits<size_t>::max() / 2)
      return Error(ENOMEM);
    size_t capacity = self.account_capacity * 2;
    auto *next = static_cast<char *>(::realloc(self.account_buffer, capacity));
    if (!next)
      return Error(ENOMEM);
    self.account_buffer = next;
    self.account_capacity = capacity;
  }
}

ErrorOr<long> WordEnvironment::read_integer(void *context,
                                            cpp::string_view name) {
  auto variable = lookup(context, name);
  if (!variable)
    return Error(variable.error());
  auto value = variable->value;
  while (!value.empty() && isspace(value.front()))
    value.remove_prefix(1);
  while (!value.empty() && isspace(value.back()))
    value.remove_suffix(1);
  if (!variable->set || value.empty())
    return 0L;
  // Required shell integer constants, not recursive evaluation of arbitrary
  // environment text. Do not inherit strtol's optional binary-prefix grammar.
  auto magnitude = value;
  if (magnitude.front() == '+' || magnitude.front() == '-')
    magnitude.remove_prefix(1);
  int base = !magnitude.empty() && magnitude.front() == '0'
                 ? (magnitude.size() > 1 &&
                            (magnitude[1] == 'x' || magnitude[1] == 'X')
                        ? 16
                        : 8)
                 : 10;
  auto number = strtointeger<long>(value.data(), base, value.size(), false);
  if (number.error)
    return Error(number.error);
  if (number.parsed_len == 0 || size_t(number.parsed_len) != value.size())
    return Error(EINVAL);
  return number.value;
}

int WordEnvironment::write_integer(void *context, cpp::string_view name,
                                   long value) {
  const IntegerToString<long> digits(value);
  return assign(context, name, digits.view());
}

WordExpansionServices WordEnvironment::services() {
  WordExpansionServices result;
  result.context = this;
  result.lookup = lookup;
  result.assign = assign;
  result.home = home;
  result.arithmetic = {this, read_integer, write_integer};
  return result;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
