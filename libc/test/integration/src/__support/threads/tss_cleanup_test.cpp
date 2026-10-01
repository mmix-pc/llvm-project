//===-- Tests for TSS generations and cleanup -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/limits_macros.h"
#include "src/__support/threads/thread.h"
#include "test/IntegrationTest/test.h"

using namespace LIBC_NAMESPACE;
static unsigned rearm_key, context_key, calls;
static int payload;

static void cleanup() {
  ThreadAttributes attributes;
  attributes.atexit_callback_mgr = internal::get_thread_atexit_callback_mgr();
  internal::call_atexit_callbacks(&attributes);
}
static void fail_destructor(void *) { ASSERT_TRUE(false); }
static void rearm(void *value) {
  ASSERT_EQ(value, &payload);
  ASSERT_EQ(get_tss_value(rearm_key), nullptr);
  ASSERT_EQ(get_tss_value(context_key), &payload);
  ++calls;
  ASSERT_TRUE(set_tss_value(rearm_key, value));
  // A callback must not hold the key registry lock.
  auto temporary = new_tss_key(nullptr);
  ASSERT_TRUE(temporary.has_value());
  ASSERT_TRUE(tss_key_delete(*temporary));
}

TEST_MAIN() {
  ASSERT_FALSE(set_tss_value(UINT_MAX, &payload));
  ASSERT_EQ(get_tss_value(UINT_MAX), nullptr);
  ASSERT_FALSE(tss_key_delete(UINT_MAX));

  unsigned keys[4096], count = 0;
  while (auto key = new_tss_key(fail_destructor)) {
    ASSERT_TRUE(count < 4096);
    for (unsigned i = 0; i != count; ++i)
      ASSERT_NE(keys[i], *key);
    keys[count++] = *key;
  }
  ASSERT_TRUE(count > 0);
  ASSERT_TRUE(set_tss_value(keys[0], &payload));
  ASSERT_TRUE(tss_key_delete(keys[0]));
  ASSERT_FALSE(tss_key_delete(keys[0]));
  ASSERT_FALSE(set_tss_value(keys[0], &payload));
  ASSERT_EQ(get_tss_value(keys[0]), nullptr);
  auto replacement = new_tss_key(fail_destructor);
  ASSERT_TRUE(replacement.has_value());
  // The only available slot is the deleted one.
  ASSERT_EQ(*replacement, keys[0]);
  ASSERT_EQ(get_tss_value(*replacement), nullptr);
  cleanup(); // Must not call either generation's destructor for stale data.
  for (unsigned i = 0; i != count; ++i)
    ASSERT_TRUE(tss_key_delete(keys[i]));

  auto context = new_tss_key(nullptr);
  auto key = new_tss_key(rearm);
  ASSERT_TRUE(context.has_value());
  ASSERT_TRUE(key.has_value());
  context_key = *context;
  rearm_key = *key;
  ASSERT_TRUE(set_tss_value(context_key, &payload));
  ASSERT_TRUE(set_tss_value(rearm_key, &payload));
  cleanup();
  ASSERT_EQ(calls, unsigned(PTHREAD_DESTRUCTOR_ITERATIONS));
  ASSERT_EQ(get_tss_value(rearm_key), nullptr);
  cleanup();
  ASSERT_EQ(calls, unsigned(PTHREAD_DESTRUCTOR_ITERATIONS));
  ASSERT_TRUE(tss_key_delete(rearm_key));
  ASSERT_TRUE(tss_key_delete(context_key));
  return 0;
}
