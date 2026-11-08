//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "in6_macro_context_test.h"

// Hermetic tests compile freestanding; the CRT expects C linkage for main.
#if !__STDC_HOSTED__
extern "C"
#endif
    int main() {
  return check_in6_macros();
}
