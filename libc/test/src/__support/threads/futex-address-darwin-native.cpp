//===-- Darwin futex address adapter with injected platform API -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/darwin/futex_utils.h"
#include <cassert>
#include <initializer_list>

// This tests adapter control flow, sizes, flags and errno preservation, not
// Darwin kernel behavior. The test driver supplies a minimal API declaration.
static int platform_errno, failure, calls;
static bool interrupt_once, shared;
static void *expected_address = reinterpret_cast<void *>(0x123400);
extern "C" int *__error() { return &platform_errno; }
int os_sync_wait_on_address(void *address, uint64_t expected, size_t size,
                            os_sync_wait_on_address_flags_t flags) {
  assert(address == expected_address && expected == 37 && size == 4);
  assert(flags == (shared ? OS_SYNC_WAIT_ON_ADDRESS_SHARED
                          : OS_SYNC_WAIT_ON_ADDRESS_NONE));
  ++calls;
  platform_errno = interrupt_once && calls == 1 ? EINTR : failure;
  return platform_errno ? -1 : 0;
}
int os_sync_wake_by_address_all(void *address, size_t size,
                                os_sync_wake_by_address_flags_t flags) {
  assert(address == expected_address && size == 4);
  assert(flags == (shared ? OS_SYNC_WAKE_BY_ADDRESS_SHARED
                          : OS_SYNC_WAKE_BY_ADDRESS_NONE));
  platform_errno = failure;
  return failure ? -1 : 3;
}
int main() {
  using LIBC_NAMESPACE::Futex;
  for (bool mode : {false, true}) {
    shared = mode;
    for (int error : {0, EFAULT, EINVAL}) {
      failure = error;
      platform_errno = EDOM;
      calls = 0;
      interrupt_once = error == 0;
      auto result = Futex::wait_at(expected_address, 37, shared);
      assert(platform_errno == EDOM);
      assert(calls == (interrupt_once ? 2 : 1));
      assert(result.has_value() == (error != EINVAL));
      if (!result.has_value())
        assert(result.error() == EINVAL);
      auto wake = Futex::notify_all_at(expected_address, shared);
      assert(platform_errno == EDOM);
      assert(wake.has_value() == (error == 0));
      if (wake.has_value())
        assert(wake.value() == 3);
      else
        assert(wake.error() == error);
    }
  }
}
