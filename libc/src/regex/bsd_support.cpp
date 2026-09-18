//===-- Libc services for the shared BSD regex engine
//----------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "hdr/func/realloc.h"
#include "src/ctype/isalnum.h"
#include "src/ctype/isalpha.h"
#include "src/ctype/isdigit.h"
#include "src/ctype/islower.h"
#include "src/ctype/isupper.h"
#include "src/ctype/tolower.h"
#include "src/ctype/toupper.h"
#include "src/string/memcmp.h"
#include "src/string/memmove.h"
#include "src/string/memset.h"
#include "src/string/strlen.h"
#include "src/string/strncmp.h"

// C sources use explicit private bridges so tests and public packaging both
// resolve to the selected libc services, not the host's public C symbols.
extern "C" {
void *__llvm_libc_regex_malloc(size_t n) { return ::malloc(n); }
void *__llvm_libc_regex_calloc(size_t n, size_t size) {
  size_t bytes;
  if (__builtin_mul_overflow(n, size, &bytes))
    return nullptr;
  void *p = ::malloc(bytes);
  if (p)
    LIBC_NAMESPACE::memset(p, 0, bytes);
  return p;
}
void *__llvm_libc_regex_realloc(void *p, size_t n) { return ::realloc(p, n); }
void __llvm_libc_regex_free(void *p) { ::free(p); }
int __llvm_libc_regex_isalpha(int c) { return LIBC_NAMESPACE::isalpha(c); }
int __llvm_libc_regex_isalnum(int c) { return LIBC_NAMESPACE::isalnum(c); }
int __llvm_libc_regex_isdigit(int c) { return LIBC_NAMESPACE::isdigit(c); }
int __llvm_libc_regex_isupper(int c) { return LIBC_NAMESPACE::isupper(c); }
int __llvm_libc_regex_islower(int c) { return LIBC_NAMESPACE::islower(c); }
int __llvm_libc_regex_tolower(int c) { return LIBC_NAMESPACE::tolower(c); }
int __llvm_libc_regex_toupper(int c) { return LIBC_NAMESPACE::toupper(c); }
size_t __llvm_libc_regex_strlen(const char *s) {
  return LIBC_NAMESPACE::strlen(s);
}
int __llvm_libc_regex_strncmp(const char *a, const char *b, size_t n) {
  return LIBC_NAMESPACE::strncmp(a, b, n);
}
void *__llvm_libc_regex_memset(void *p, int c, size_t n) {
  return LIBC_NAMESPACE::memset(p, c, n);
}
void *__llvm_libc_regex_memmove(void *d, const void *s, size_t n) {
  return LIBC_NAMESPACE::memmove(d, s, n);
}
int __llvm_libc_regex_memcmp(const void *a, const void *b, size_t n) {
  return LIBC_NAMESPACE::memcmp(a, b, n);
}
}
