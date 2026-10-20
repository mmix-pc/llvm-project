//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_RESOLV_MACROS_H
#define LLVM_LIBC_MACROS_RESOLV_MACROS_H

#include "arpa-nameser-macros.h"

#define PACKETSZ NS_PACKETSZ
#define MAXDNAME NS_MAXDNAME
#define MAXCDNAME NS_MAXCDNAME
#define MAXLABEL NS_MAXLABEL
#define HFIXEDSZ NS_HFIXEDSZ
#define QFIXEDSZ NS_QFIXEDSZ
#define RRFIXEDSZ NS_RRFIXEDSZ
#define INT16SZ NS_INT16SZ
#define INT32SZ NS_INT32SZ
#define QUERY 0
#define IQUERY 1
#define STATUS 2
#define C_IN 1
#define C_CHAOS 3
#define C_HS 4
#define C_ANY 255
#define T_A 1
#define T_NS 2
#define T_CNAME 5
#define T_SOA 6
#define T_PTR 12
#define T_MX 15
#define T_TXT 16
#define T_AAAA 28
#define T_SRV 33
#define T_ANY 255

#endif // LLVM_LIBC_MACROS_RESOLV_MACROS_H
