//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Macros defined in net/if.h header file.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_LINUX_NET_IF_MACROS_H
#define LLVM_LIBC_MACROS_LINUX_NET_IF_MACROS_H

#define IF_NAMESIZE 16

#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_DEFAULT)
#ifndef IFNAMSIZ
#define IFNAMSIZ IF_NAMESIZE
#undef __UAPI_DEF_IF_IFNAMSIZ
#define __UAPI_DEF_IF_IFNAMSIZ 0
#endif
#ifndef IFHWADDRLEN
#define IFHWADDRLEN 6
#endif
#ifndef IFF_UP
#define IFF_UP (1U << 0)
#define IFF_BROADCAST (1U << 1)
#define IFF_DEBUG (1U << 2)
#define IFF_LOOPBACK (1U << 3)
#define IFF_POINTOPOINT (1U << 4)
#define IFF_NOTRAILERS (1U << 5)
#define IFF_RUNNING (1U << 6)
#define IFF_NOARP (1U << 7)
#define IFF_PROMISC (1U << 8)
#define IFF_ALLMULTI (1U << 9)
#define IFF_MASTER (1U << 10)
#define IFF_SLAVE (1U << 11)
#define IFF_MULTICAST (1U << 12)
#define IFF_PORTSEL (1U << 13)
#define IFF_AUTOMEDIA (1U << 14)
#define IFF_DYNAMIC (1U << 15)
#define IFF_LOWER_UP (1U << 16)
#define IFF_DORMANT (1U << 17)
#define IFF_ECHO (1U << 18)
#undef __UAPI_DEF_IF_NET_DEVICE_FLAGS
#define __UAPI_DEF_IF_NET_DEVICE_FLAGS 0
#undef __UAPI_DEF_IF_NET_DEVICE_FLAGS_LOWER_UP_DORMANT_ECHO
#define __UAPI_DEF_IF_NET_DEVICE_FLAGS_LOWER_UP_DORMANT_ECHO 0
#endif
#endif

#endif // LLVM_LIBC_MACROS_LINUX_NET_IF_MACROS_H
