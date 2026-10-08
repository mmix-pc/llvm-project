//===-- MMIX Linux mutex protocol attributes ---------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_MUTEX_PROTOCOL_H
#define LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_MUTEX_PROTOCOL_H
#include "src/pthread/pthread_mutexattr.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
constexpr unsigned MUTEX_PROTOCOL_SHIFT = 4;
constexpr unsigned MUTEX_PROTOCOL_MASK = 3 << MUTEX_PROTOCOL_SHIFT;
constexpr unsigned MUTEX_ATTR_CEILING_SHIFT = 6;
constexpr unsigned MUTEX_ATTR_CEILING_MASK = 127 << MUTEX_ATTR_CEILING_SHIFT;
inline int mutex_attr_ceiling(pthread_mutexattr_t attr) {
  return int((attr & MUTEX_ATTR_CEILING_MASK) >> MUTEX_ATTR_CEILING_SHIFT);
}
inline int mutex_protocol(pthread_mutexattr_t attr) {
  return (attr & MUTEX_PROTOCOL_MASK) >> MUTEX_PROTOCOL_SHIFT;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
