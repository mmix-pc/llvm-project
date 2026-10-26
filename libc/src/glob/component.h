//===-- Locale-aware glob component parsing ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_GLOB_COMPONENT_H
#define LLVM_LIBC_SRC_GLOB_COMPONENT_H

#include "src/__support/wchar/restartable_converter.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
struct GlobComponent {
  bool magic;
  size_t length;
};

// A component is a filesystem byte string without '/'. Validate it before
// changing storage; dequote only literal components, retaining shift bytes.
// The caller supplies length + 1 bytes, including the terminating NUL.
ErrorOr<GlobComponent> prepare_glob_component(char *component, size_t length,
                                              bool noescape,
                                              bool followed_slash,
                                              RestartableConverter converter);
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
