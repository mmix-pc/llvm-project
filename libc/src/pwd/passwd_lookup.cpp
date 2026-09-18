//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/pwd/lookup.h"

#ifndef LIBC_COPT_PWD_FILE_PATH
#define LIBC_COPT_PWD_FILE_PATH "/etc/passwd"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace pwd {
static const char *path = LIBC_COPT_PWD_FILE_PATH;
internal::AccountBuffer<struct passwd> lookup_buffer;
const char *lookup_path() { return path; }
void TESTONLY_set_lookup_path(const char *value) {
  path = value ? value : LIBC_COPT_PWD_FILE_PATH;
}
} // namespace pwd
} // namespace LIBC_NAMESPACE_DECL
