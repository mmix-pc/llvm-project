//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/new.h"
#include "src/__support/ctype_utils.h"
#include "src/__support/str_to_integer.h"
#include "src/grp/lookup.h"
#include "src/pwd/field_tokenizer.h"

#ifndef LIBC_COPT_GROUP_FILE_PATH
#define LIBC_COPT_GROUP_FILE_PATH "/etc/group"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace group_db {
static const char *path = LIBC_COPT_GROUP_FILE_PATH;
internal::AccountBuffer<struct group> lookup_buffer;
const char *lookup_path() { return path; }
void TESTONLY_set_lookup_path(const char *value) {
  path = value ? value : LIBC_COPT_GROUP_FILE_PATH;
}

int parse_group(char *buffer, size_t length, size_t size, struct group *entry) {
  if (!length || length > size)
    return ERANGE;
  pwd::FieldTokenizer fields(cpp::span<char>(buffer, length));
  auto name = fields.next_field();
  auto password = fields.next_field();
  auto gid = fields.next_field();
  auto members = fields.next_field();
  if (!name || name->size() <= 1 || !password || !gid || !members ||
      fields.next_field())
    return EINVAL;
  if (gid->size() <= 1 || !internal::isdigit(gid->front()))
    return EINVAL;
  auto number = internal::strtointeger<gid_t>(gid->data(), 10);
  if (number.has_error() ||
      static_cast<size_t>(number.parsed_len) != gid->size() - 1)
    return EINVAL;

  size_t count = members->front() ? 1 : 0;
  for (size_t i = 0; i + 1 < members->size(); ++i) {
    if ((*members)[i] == ',') {
      if (i == 0 || (*members)[i - 1] == ',' || !(*members)[i + 1])
        return EINVAL;
      ++count;
    }
  }
  // Caller buffers need not be pointer-aligned. Place the member pointer array
  // after the complete record and account for alignment before writing it.
  size_t padding =
      (-reinterpret_cast<uintptr_t>(buffer + length)) % alignof(char *);
  if (padding > size - length)
    return ERANGE;
  size_t offset = length + padding;
  size_t slots = (size - offset) / sizeof(char *);
  if (count >= slots)
    return ERANGE;
  auto pointers = reinterpret_cast<char **>(buffer + offset);
  size_t index = 0;
  if (count) {
    ::new (pointers + index++) char *(members->data());
    for (char &ch : *members)
      if (ch == ',') {
        ch = '\0';
        ::new (pointers + index++) char *(&ch + 1);
      }
  }
  ::new (pointers + index) char *(nullptr);
  *entry = {name->data(), password->data(), number.value, pointers};
  return 0;
}
} // namespace group_db
} // namespace LIBC_NAMESPACE_DECL
