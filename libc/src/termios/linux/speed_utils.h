//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Speed translation utilities for termios.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_TERMIOS_LINUX_SPEED_UTILS_H
#define LLVM_LIBC_SRC_TERMIOS_LINUX_SPEED_UTILS_H

#include "hdr/termios_macros.h"
#include "hdr/types/speed_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

constexpr speed_t INVALID_SPEED = ~static_cast<speed_t>(0);

// glibc 2.42 changed the behavior of the termios Bxxx constants:
// - Before glibc 2.42, the Bxxx constants correspond to the kernel speed
// bitmasks
//   directly (e.g. B50 = 1).
// - Starting from glibc 2.42, to support arbitrary baud rates numerically, the
//   Bxxx constants are defined as their actual integer values (e.g. B50 = 50).
// In this case, we need to translate between the host's numerical speeds and
// the kernel's speed bitmasks.
// - If B50 == 1, then the host uses the kernel's bitmasks directly (no
//   translation).
// - If B50 is not 1 (e.g. B50 == 50 in glibc >= 2.42), we apply
//   translation.
#if (B50 == 1)

LIBC_INLINE constexpr speed_t encode_speed(speed_t speed) { return speed; }
LIBC_INLINE constexpr speed_t decode_speed(speed_t speed) { return speed; }

#else // Overlay mode with numerical speeds (e.g. glibc)

LIBC_INLINE constexpr speed_t encode_speed(speed_t speed) {
  switch (speed) {
  case 0:
    return 0;
  case 50:
    return 0000001;
  case 75:
    return 0000002;
  case 110:
    return 0000003;
  case 134:
    return 0000004;
  case 150:
    return 0000005;
  case 200:
    return 0000006;
  case 300:
    return 0000007;
  case 600:
    return 0000010;
  case 1200:
    return 0000011;
  case 1800:
    return 0000012;
  case 2400:
    return 0000013;
  case 4800:
    return 0000014;
  case 9600:
    return 0000015;
  case 19200:
    return 0000016;
  case 38400:
    return 0000017;
  case 57600:
    return 0010001;
  case 115200:
    return 0010002;
  case 230400:
    return 0010003;
  case 460800:
    return 0010004;
  case 500000:
    return 0010005;
  case 576000:
    return 0010006;
  case 921600:
    return 0010007;
  case 1000000:
    return 0010010;
  case 1152000:
    return 0010011;
  case 1500000:
    return 0010012;
  case 2000000:
    return 0010013;
  case 2500000:
    return 0010014;
  case 3000000:
    return 0010015;
  case 3500000:
    return 0010016;
  case 4000000:
    return 0010017;
  default:
    return INVALID_SPEED;
  }
}

LIBC_INLINE constexpr speed_t decode_speed(speed_t kernel_speed) {
  switch (kernel_speed) {
  case 0:
    return 0;
  case 0000001:
    return 50;
  case 0000002:
    return 75;
  case 0000003:
    return 110;
  case 0000004:
    return 134;
  case 0000005:
    return 150;
  case 0000006:
    return 200;
  case 0000007:
    return 300;
  case 0000010:
    return 600;
  case 0000011:
    return 1200;
  case 0000012:
    return 1800;
  case 0000013:
    return 2400;
  case 0000014:
    return 4800;
  case 0000015:
    return 9600;
  case 0000016:
    return 19200;
  case 0000017:
    return 38400;
  case 0010001:
    return 57600;
  case 0010002:
    return 115200;
  case 0010003:
    return 230400;
  case 0010004:
    return 460800;
  case 0010005:
    return 500000;
  case 0010006:
    return 576000;
  case 0010007:
    return 921600;
  case 0010010:
    return 1000000;
  case 0010011:
    return 1152000;
  case 0010012:
    return 1500000;
  case 0010013:
    return 2000000;
  case 0010014:
    return 2500000;
  case 0010015:
    return 3000000;
  case 0010016:
    return 3500000;
  case 0010017:
    return 4000000;
  default:
    return kernel_speed;
  }
}

#endif // (B50 == 1)

LIBC_INLINE constexpr bool is_valid_speed(speed_t speed) {
  const speed_t encoded = encode_speed(speed);
  if ((encoded & ~static_cast<speed_t>(CBAUD)) != 0)
    return false;
#if B50 != 1
  return encoded != INVALID_SPEED;
#else
  // CBAUD also contains unnamed encodings (including BOTHER). Use the public
  // names because extended rates have different encodings on some platforms.
  switch (speed) {
  case B0:
  case B50:
  case B75:
  case B110:
  case B134:
  case B150:
  case B200:
  case B300:
  case B600:
  case B1200:
  case B1800:
  case B2400:
  case B4800:
  case B9600:
  case B19200:
  case B38400:
#ifdef B57600
  case B57600:
#endif
#ifdef B76800
  case B76800:
#endif
#ifdef B115200
  case B115200:
#endif
#ifdef B153600
  case B153600:
#endif
#ifdef B230400
  case B230400:
#endif
#ifdef B307200
  case B307200:
#endif
#ifdef B460800
  case B460800:
#endif
#ifdef B500000
  case B500000:
#endif
#ifdef B576000
  case B576000:
#endif
#ifdef B614400
  case B614400:
#endif
#ifdef B921600
  case B921600:
#endif
#ifdef B1000000
  case B1000000:
#endif
#ifdef B1152000
  case B1152000:
#endif
#ifdef B1500000
  case B1500000:
#endif
#ifdef B2000000
  case B2000000:
#endif
#ifdef B2500000
  case B2500000:
#endif
#ifdef B3000000
  case B3000000:
#endif
#ifdef B3500000
  case B3500000:
#endif
#ifdef B4000000
  case B4000000:
#endif
    return true;
  default:
    return false;
  }
#endif
}

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_TERMIOS_LINUX_SPEED_UTILS_H
