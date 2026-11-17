//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SCSI_SG_H
#define LLVM_LIBC_SCSI_SG_H
#include <stddef.h>

// Linux SCSI generic version 3 user/kernel request layout.
typedef struct sg_iovec {
  void *iov_base;
  size_t iov_len;
} sg_iovec_t;
typedef struct sg_io_hdr {
  int interface_id;
  int dxfer_direction;
  unsigned char cmd_len;
  unsigned char mx_sb_len;
  unsigned short iovec_count;
  unsigned int dxfer_len;
  void *dxferp;
  unsigned char *cmdp;
  unsigned char *sbp;
  unsigned int timeout;
  unsigned int flags;
  int pack_id;
  void *usr_ptr;
  unsigned char status;
  unsigned char masked_status;
  unsigned char msg_status;
  unsigned char sb_len_wr;
  unsigned short host_status;
  unsigned short driver_status;
  int resid;
  unsigned int duration;
  unsigned int info;
} sg_io_hdr_t;

#define SG_DXFER_NONE (-1)
#define SG_DXFER_TO_DEV (-2)
#define SG_DXFER_FROM_DEV (-3)
#define SG_DXFER_TO_FROM_DEV (-4)
#define SG_GET_VERSION_NUM 0x2282
#define SG_IO 0x2285
#endif
