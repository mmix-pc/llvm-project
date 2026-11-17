//===-- Loader diagnostic state regression --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstring>
#include "src/dlfcn/linux/mmix/loading.cpp"

namespace LIBC_NAMESPACE_DECL::mmix {
loader::LoaderCancellation loader_cancellation_hooks() {
  return {+[](uint64_t *saved) -> long { *saved = 0; return 0; },
          +[](uint64_t) -> long { return 0; }};
}
}
namespace M = LIBC_NAMESPACE::mmix;
namespace L = LIBC_NAMESPACE::loader;
static long failure;
static bool nested_failure;
static long open_image(void *, const char *, int, uintptr_t,
                       L::LoaderCancellation, uint64_t *token) {
  *token = 1;
  if (nested_failure) {
    failure = ENOENT;
    M::loader_lookup(reinterpret_cast<void *>(1), "nested", 1);
    failure = 0;
  }
  return failure;
}
static long lookup(void *, uint64_t, const char *, uintptr_t,
                   L::LoaderCancellation, uintptr_t *value) {
  *value = 0; // A successful null-valued symbol is not an error.
  return failure;
}
static long close_image(void *, uint64_t, L::LoaderCancellation) { return failure; }
int main() {
  assert(M::adopt_loading_services({&failure, open_image, lookup, close_image}));
  assert(M::loader_error() == nullptr);
  auto handle = reinterpret_cast<void *>(1);
  failure = ENOENT;
  assert(M::loader_lookup(handle, "absent", 1) == nullptr);
  failure = 0;
  assert(M::loader_lookup(handle, "zero", 1) == nullptr);
  const char *first = M::loader_error();
  assert(first && std::strstr(first, "not found"));
  assert(!M::loader_error());
  char saved[96];
  std::strcpy(saved, first);
  failure = ENOMEM;
  assert(M::loader_lookup(handle, "failed", 1) == nullptr);
  assert(std::strcmp(first, saved) == 0);
  const char *second = M::loader_error();
  assert(second && std::strstr(second, "allocation"));
  assert(!M::loader_error());
  failure = ENOENT;
  assert(!M::loader_lookup(handle, "absent", 1));
  failure = 0;
  assert(M::loader_open(nullptr, L::OpenNow, 1) == handle);
  assert(M::loader_close(handle) == 0);
  assert(M::loader_error() && !M::loader_error());
  assert(!M::loader_lookup(handle, "zero", 1) && !M::loader_error());
  nested_failure = true;
  assert(M::loader_open(nullptr, L::OpenNow, 1) == handle);
  assert(M::loader_error() && !M::loader_error());
}
