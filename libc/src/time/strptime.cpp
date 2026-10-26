//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definition of the strptime function.
///
//===----------------------------------------------------------------------===//

#include "src/time/strptime.h"
#include "hdr/types/struct_tm.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/time/strptime_parser.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(char *, strptime,
                   (const char *__restrict buf, const char *__restrict format,
                    struct tm *__restrict tm)) {
  internal::StrptimeParser parser;
  const char *input = buf;
  size_t available = cpp::numeric_limits<size_t>::max();
  if (!parser.parse(input, format, available) || !parser.finish())
    return nullptr;
  parser.commit(*tm);
  return const_cast<char *>(input);
}

} // namespace LIBC_NAMESPACE_DECL
