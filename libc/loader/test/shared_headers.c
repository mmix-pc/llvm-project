//===-- Shared loading header signatures ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Compile as C and C++ against the generated shared-libc headers.
#include <dlfcn.h>
#include <link.h>

void *(*open_image)(const char *, int) = dlopen;
void *(*find_symbol)(void *, const char *) = dlsym;
int (*close_image)(void *) = dlclose;
char *(*last_error)(void) = dlerror;
int (*address_info)(const void *, Dl_info *) = dladdr;
int (*handle_info)(void *, int, void *) = dlinfo;
int (*enumerate_images)(int (*)(struct dl_phdr_info *, size_t, void *), void *) =
    dl_iterate_phdr;
