//===-- MMIX Linux reboot declarations ----------------------------*- C -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_MMIX_REBOOT_H
#define LLVM_LIBC_MMIX_REBOOT_H

#include <__llvm-libc-common.h>
#include <linux/reboot.h>

#define RB_AUTOBOOT LINUX_REBOOT_CMD_RESTART
#define RB_HALT_SYSTEM LINUX_REBOOT_CMD_HALT
#define RB_ENABLE_CAD LINUX_REBOOT_CMD_CAD_ON
#define RB_DISABLE_CAD LINUX_REBOOT_CMD_CAD_OFF
#define RB_POWER_OFF LINUX_REBOOT_CMD_POWER_OFF
#define RB_SW_SUSPEND LINUX_REBOOT_CMD_SW_SUSPEND
#define RB_KEXEC LINUX_REBOOT_CMD_KEXEC

__BEGIN_C_DECLS
int reboot(int) __NOEXCEPT;
__END_C_DECLS

#endif
