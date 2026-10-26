//===-- Locale-aware pathname pattern matching ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_FNMATCH_MATCHER_H
#define LLVM_LIBC_SRC_FNMATCH_MATCHER_H

#include "src/__support/wchar/restartable_converter.h"
#include "src/locale/character.h"
#include "src/locale/collation_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// All category views remain borrowed for the complete call. This does not
// change thread/global locale or retain pattern/input storage after returning.
ErrorOr<bool> match_path_pattern(cpp::string_view pattern,
                                 cpp::string_view input, int flags,
                                 RestartableConverter converter,
                                 const __locale_data *ctype,
                                 const CollationData *collation);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
