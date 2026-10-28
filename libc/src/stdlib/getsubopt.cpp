//===-- Implementation of getsubopt ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/getsubopt.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, getsubopt,
                   (char **__restrict optionp, char *const *__restrict tokens,
                    char **__restrict valuep)) {
  char *start = *optionp;
  char *end = start;
  char *equals = nullptr;
  while (*end != '\0' && *end != ',') {
    if (*end == '=' && equals == nullptr)
      equals = end;
    ++end;
  }
  char *key_end = equals ? equals : end;
  *valuep = equals ? equals + 1 : nullptr;
  *optionp = *end == ',' ? end + 1 : end;
  *end = '\0';

  for (int index = 0; tokens[index] != nullptr; ++index) {
    const char *key = tokens[index];
    const char *input = start;
    while (input != key_end && *key != '\0' && *input == *key) {
      ++input;
      ++key;
    }
    if (input == key_end && *key == '\0')
      return index;
  }
  return -1;
}

} // namespace LIBC_NAMESPACE_DECL
