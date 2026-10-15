//===-- Public runtime loading header probe -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>

void *(*open_pointer)(const char *, int) = dlopen;
void *(*lookup_pointer)(void *, const char *) = dlsym;
char *(*error_pointer)(void) = dlerror;
int (*close_pointer)(void *) = dlclose;
#ifdef __cplusplus
#define CHECK static_assert
#else
#define CHECK _Static_assert
#endif
CHECK(RTLD_LAZY == 1 && RTLD_NOW == 2 && RTLD_NOLOAD == 4, "binding flags");
CHECK(RTLD_GLOBAL == 0x100 && RTLD_LOCAL == 0, "scope flags");
CHECK(RTLD_NODELETE == 0x1000 && RTLD_DEEPBIND == 8, "lifetime/rejected flags");
void *default_scope = RTLD_DEFAULT;
void *next_scope = RTLD_NEXT;
