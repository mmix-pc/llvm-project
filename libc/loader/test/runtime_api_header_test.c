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
#include <link.h>

void *(*open_pointer)(const char *, int) = dlopen;
void *(*lookup_pointer)(void *, const char *) = dlsym;
char *(*error_pointer)(void) = dlerror;
int (*close_pointer)(void *) = dlclose;
int (*address_pointer)(const void *, Dl_info *) = dladdr;
int (*info_pointer)(void *, int, void *) = dlinfo;
int (*iterate_pointer)(int (*)(struct dl_phdr_info *, size_t, void *), void *) = dl_iterate_phdr;
#ifdef __cplusplus
#define CHECK static_assert
#else
#define CHECK _Static_assert
#endif
CHECK(RTLD_LAZY == 1 && RTLD_NOW == 2 && RTLD_NOLOAD == 4, "binding flags");
CHECK(RTLD_GLOBAL == 0x100 && RTLD_LOCAL == 0, "scope flags");
CHECK(RTLD_NODELETE == 0x1000 && RTLD_DEEPBIND == 8, "lifetime/rejected flags");
CHECK(RTLD_DI_LINKMAP == 2 && RTLD_DI_TLS_MODID == 9 && RTLD_DI_TLS_DATA == 10, "query requests");
CHECK(sizeof(struct link_map) == 40 && sizeof(struct r_debug) == 40, "debugger ABI");
void *default_scope = RTLD_DEFAULT;
void *next_scope = RTLD_NEXT;
