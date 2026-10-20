//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/limits.h"
#include "src/pwd/field_tokenizer.h"
#include "src/shadow/lookup.h"

namespace LIBC_NAMESPACE_DECL {
namespace shadow_db {
static const char *path = "/etc/shadow";
internal::AccountBuffer<struct spwd> lookup_buffer;
const char *lookup_path() { return path; }
void TESTONLY_set_lookup_path(const char *value) {
  path = value ? value : "/etc/shadow";
}

namespace {
// Fields include their terminating NUL. Never scan beyond the supplied span.
template <typename T> bool parse_number(cpp::span<char> field, T &value) {
  if (field.size() == 1) {
    value = T(-1);
    return true;
  }
  if constexpr (cpp::numeric_limits<T>::is_signed) {
    if (field.size() == 3 && field[0] == '-' && field[1] == '1') {
      value = -1;
      return true;
    }
  }
  T number = 0;
  constexpr T limit = cpp::numeric_limits<T>::max();
  for (size_t i = 0; i + 1 < field.size(); ++i) {
    if (field[i] < '0' || field[i] > '9')
      return false;
    T digit = field[i] - '0';
    if (number > (limit - digit) / 10)
      return false;
    number = number * 10 + digit;
  }
  value = number;
  return true;
}
} // namespace

int parse_shadow(char *buffer, size_t length, size_t size, struct spwd *entry) {
  if (!length || length > size)
    return ERANGE;
  if (buffer[length - 1] != '\0')
    return EINVAL;
  for (size_t i = 0; i + 1 < length; ++i)
    if (buffer[i] == '\0')
      return EINVAL;

  pwd::FieldTokenizer fields(cpp::span<char>(buffer, length));
  auto name = fields.next_field();
  auto password = fields.next_field();
  if (!name || name->size() <= 1 || name->front() == '#' || !password)
    return EINVAL;

  struct spwd candidate{};
  candidate.sp_namp = name->data();
  candidate.sp_pwdp = password->data();
  long *numbers[] = {&candidate.sp_lstchg, &candidate.sp_min,
                     &candidate.sp_max,    &candidate.sp_warn,
                     &candidate.sp_inact,  &candidate.sp_expire};
  for (long *number : numbers) {
    auto field = fields.next_field();
    if (!field || !parse_number(*field, *number))
      return EINVAL;
  }
  auto flag = fields.next_field();
  if (!flag || !parse_number(*flag, candidate.sp_flag) || fields.next_field())
    return EINVAL;
  *entry = candidate;
  return 0;
}
} // namespace shadow_db
} // namespace LIBC_NAMESPACE_DECL
