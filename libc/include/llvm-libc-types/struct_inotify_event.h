// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_TYPES_STRUCT_INOTIFY_EVENT_H
#define LLVM_LIBC_TYPES_STRUCT_INOTIFY_EVENT_H
struct inotify_event {
  int wd;
  __UINT32_TYPE__ mask;
  __UINT32_TYPE__ cookie;
  __UINT32_TYPE__ len;
  char name[];
};
#endif
