//===-- Word expansion result composition -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_EVALUATE_H
#define LLVM_LIBC_SRC_WORDEXP_EVALUATE_H

#include "src/wordexp/expand.h"
#include "src/wordexp/results.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

struct WordEvaluationResult {
  int status = 0; // Zero or a WRDE_* value.
  int error = 0;  // An additional system cause, if one is available.
};

// The enclosing entrypoint retains the selected locale and supplies actual
// environment/process services. Glob must use that same locale. No callback is
// invoked before complete syntax/NOCMD admission. IFS is obtained for each
// expanded word so selected assignments are visible. Resources are invocation
// owned; flags and partial-result publication use WordResultTransaction.
// This component does not export wordexp or establish installed qualification.
WordEvaluationResult evaluate_wordexp(cpp::string_view input, wordexp_t &result,
                                      int flags, RestartableConverter converter,
                                      const __locale_data *ctype,
                                      const CollationData *collation,
                                      WordExpansionServices services);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
