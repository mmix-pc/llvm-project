//===-- Linux word expansion services ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_RUNTIME_H
#define LLVM_LIBC_SRC_WORDEXP_RUNTIME_H

#include "src/wordexp/evaluate.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Real environment, account and process services for the private composition
// entry. The caller retains the selected locale and synchronizes environment
// mutation. This does not enable the unfinished public provider.
WordEvaluationResult evaluate_wordexp_runtime(cpp::string_view input,
                                              wordexp_t &result, int flags,
                                              RestartableConverter converter,
                                              const __locale_data *ctype,
                                              const CollationData *collation);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
