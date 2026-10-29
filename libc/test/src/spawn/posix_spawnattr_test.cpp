//===-- Unittests for posix_spawnattr_t -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/sched_macros.h"
#include "hdr/types/struct_sched_param.h"
#include "src/__support/libc_errno.h"
#include "src/spawn/posix_spawnattr_destroy.h"
#include "src/spawn/posix_spawnattr_getflags.h"
#include "src/spawn/posix_spawnattr_getpgroup.h"
#include "src/spawn/posix_spawnattr_getschedparam.h"
#include "src/spawn/posix_spawnattr_getschedpolicy.h"
#include "src/spawn/posix_spawnattr_getsigdefault.h"
#include "src/spawn/posix_spawnattr_getsigmask.h"
#include "src/spawn/posix_spawnattr_init.h"
#include "src/spawn/posix_spawnattr_setflags.h"
#include "src/spawn/posix_spawnattr_setpgroup.h"
#include "src/spawn/posix_spawnattr_setschedparam.h"
#include "src/spawn/posix_spawnattr_setschedpolicy.h"
#include "src/spawn/posix_spawnattr_setsigdefault.h"
#include "src/spawn/posix_spawnattr_setsigmask.h"
#include "test/UnitTest/Test.h"

#include <spawn.h>

#if defined(__linux__) && defined(__mmix__)
static_assert(sizeof(sigset_t) == 128);
static_assert(sizeof(posix_spawnattr_t) == 272);
#endif

namespace {

constexpr short ALL_FLAGS = POSIX_SPAWN_RESETIDS | POSIX_SPAWN_SETPGROUP |
                            POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK |
                            POSIX_SPAWN_SETSCHEDPARAM |
                            POSIX_SPAWN_SETSCHEDULER | POSIX_SPAWN_SETSID;
constexpr unsigned SIGNAL_WORDS = sizeof(sigset_t) / sizeof(unsigned long);

class LlvmLibcPosixSpawnAttrTest : public LIBC_NAMESPACE::testing::Test {
  int saved_errno;

public:
  posix_spawnattr_t attr;

  void SetUp() override {
    saved_errno = libc_errno;
    libc_errno = EDOM;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_init(&attr), 0);
  }

  void TearDown() override {
    EXPECT_EQ(LIBC_NAMESPACE::posix_spawnattr_destroy(&attr), 0);
    EXPECT_EQ(int(libc_errno), EDOM);
    libc_errno = saved_errno;
  }

  void checkDefaults() {
    short flags = -1;
    pid_t pgroup = -1;
    sigset_t sigdefault;
    for (auto &word : sigdefault.__signals)
      word = ~0UL;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getflags(&attr, &flags), 0);
    EXPECT_EQ(flags, short(0));
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getpgroup(&attr, &pgroup), 0);
    EXPECT_EQ(pgroup, pid_t(0));
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getsigdefault(&attr, &sigdefault),
              0);
    for (auto word : sigdefault.__signals)
      EXPECT_EQ(word, 0UL);
    // POSIX does not specify initial policy, priority or mask values.
  }

  void checkSignals(const sigset_t &expected_default,
                    const sigset_t &expected_mask) {
    struct GuardedSet {
      unsigned long before = 123;
      sigset_t set = {};
      unsigned long after = 456;
    } defaults, mask;
    ASSERT_EQ(
        LIBC_NAMESPACE::posix_spawnattr_getsigdefault(&attr, &defaults.set), 0);
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getsigmask(&attr, &mask.set), 0);
    for (unsigned i = 0; i < SIGNAL_WORDS; ++i) {
      EXPECT_EQ(defaults.set.__signals[i], expected_default.__signals[i]);
      EXPECT_EQ(mask.set.__signals[i], expected_mask.__signals[i]);
    }
    EXPECT_EQ(defaults.before, 123UL);
    EXPECT_EQ(defaults.after, 456UL);
    EXPECT_EQ(mask.before, 123UL);
    EXPECT_EQ(mask.after, 456UL);
  }
};

} // namespace

TEST_F(LlvmLibcPosixSpawnAttrTest, DefaultsAndReinitialization) {
  checkDefaults();
  for (int cycle = 0; cycle < 2; ++cycle) {
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setflags(&attr, ALL_FLAGS), 0);
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setpgroup(&attr, 17), 0);
    struct sched_param param = {23};
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setschedparam(&attr, &param), 0);
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setschedpolicy(&attr, SCHED_FIFO),
              0);
    sigset_t set;
    for (auto &word : set.__signals)
      word = ~0UL;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setsigdefault(&attr, &set), 0);
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setsigmask(&attr, &set), 0);
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_destroy(&attr), 0);
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_init(&attr), 0);
    checkDefaults();
  }
}

TEST_F(LlvmLibcPosixSpawnAttrTest, FlagsAndFailurePreservation) {
  // Every subset is storable. SETSID | SETPGROUP has unspecified child effects,
  // but that is not an error in storing the attribute object.
  for (int flags = 0; flags <= ALL_FLAGS; ++flags) {
    if (flags & ~ALL_FLAGS)
      continue;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setflags(&attr, short(flags)), 0);
    short result = -1;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getflags(&attr, &result), 0);
    EXPECT_EQ(result, short(flags));
  }

  for (unsigned bit = 1; bit <= unsigned(SHRT_MAX); bit <<= 1) {
    if (bit & ALL_FLAGS)
      continue;
    EXPECT_EQ(
        LIBC_NAMESPACE::posix_spawnattr_setflags(&attr, short(bit | ALL_FLAGS)),
        EINVAL);
    EXPECT_EQ(int(libc_errno), EDOM);
    short flags = 0;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getflags(&attr, &flags), 0);
    EXPECT_EQ(flags, ALL_FLAGS);
  }
  const short invalid_flags[] = {SHRT_MIN, -1};
  for (short invalid : invalid_flags) {
    EXPECT_EQ(LIBC_NAMESPACE::posix_spawnattr_setflags(&attr, invalid), EINVAL);
    short flags = 0;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getflags(&attr, &flags), 0);
    EXPECT_EQ(flags, ALL_FLAGS);
  }
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setflags(&attr, 0), 0);
  short flags = -1;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getflags(&attr, &flags), 0);
  EXPECT_EQ(flags, short(0));
}

TEST_F(LlvmLibcPosixSpawnAttrTest, ScalarStorageAndSchedulingPolicy) {
  // Like glibc, scalar setters store values without checking whether the child
  // can use them. In particular, priority may be set before policy.
  const pid_t groups[] = {0, 17, -1};
  for (pid_t value : groups) {
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setpgroup(&attr, value), 0);
    pid_t result = 42;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getpgroup(&attr, &result), 0);
    EXPECT_EQ(result, value);
  }
  const int priorities[] = {INT_MIN, -1, 0, 1, 99, INT_MAX};
  const int policies[] = {SCHED_OTHER, SCHED_FIFO, SCHED_RR};
  for (int priority : priorities) {
    struct sched_param input = {priority};
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setschedparam(&attr, &input), 0);
    input.sched_priority = 42;
    for (int policy : policies) {
      ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setschedpolicy(&attr, policy),
                0);
      int result = -1;
      ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getschedpolicy(&attr, &result),
                0);
      EXPECT_EQ(result, policy);
      struct sched_param output = {42};
      ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getschedparam(&attr, &output),
                0);
      EXPECT_EQ(output.sched_priority, priority);
    }
  }

  const int invalid_policies[] = {-1,          INT_MIN,    INT_MAX,
                                  SCHED_BATCH, SCHED_IDLE, SCHED_DEADLINE};
  for (int invalid : invalid_policies) {
    EXPECT_EQ(LIBC_NAMESPACE::posix_spawnattr_setschedpolicy(&attr, invalid),
              EINVAL);
    EXPECT_EQ(int(libc_errno), EDOM);
    int policy = -1;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getschedpolicy(&attr, &policy),
              0);
    EXPECT_EQ(policy, SCHED_RR);
    struct sched_param param;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getschedparam(&attr, &param), 0);
    EXPECT_EQ(param.sched_priority, INT_MAX);
  }
  short flags = -1;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getflags(&attr, &flags), 0);
  EXPECT_EQ(flags, short(0));
  pid_t group = 0;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getpgroup(&attr, &group), 0);
  EXPECT_EQ(group, pid_t(-1));
}

TEST_F(LlvmLibcPosixSpawnAttrTest, CompletePublicSignalSetCopies) {
  // This representation-specific regression covers all public words, including
  // MMIX's tail beyond the kernel mask. Object storage must not filter private,
  // uncatchable or unblockable signal bits; application is a separate
  // operation.
  sigset_t expected_default, expected_mask;
  for (unsigned i = 0; i < SIGNAL_WORDS; ++i) {
    expected_default.__signals[i] = ~0UL - i;
    expected_mask.__signals[i] = 0x55555555UL + i;
  }
  {
    sigset_t input = expected_default;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setsigdefault(&attr, &input), 0);
    input = expected_mask;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setsigmask(&attr, &input), 0);
    input = {};
  }
  checkSignals(expected_default, expected_mask);

  sigset_t output;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getsigdefault(&attr, &output), 0);
  output = {};
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getsigmask(&attr, &output), 0);
  output = {};
  checkSignals(expected_default, expected_mask);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setsigdefault(&attr, &output), 0);
  checkSignals(output, expected_mask);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setsigmask(&attr, &output), 0);
  checkSignals(output, output);
}

TEST_F(LlvmLibcPosixSpawnAttrTest, IndependentObjects) {
  posix_spawnattr_t other;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_init(&other), 0);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setflags(&attr, ALL_FLAGS), 0);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setpgroup(&attr, 17), 0);
  struct sched_param param = {23};
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setschedparam(&attr, &param), 0);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setschedpolicy(&attr, SCHED_FIFO),
            0);
  sigset_t set;
  for (auto &word : set.__signals)
    word = ~0UL;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setsigdefault(&attr, &set), 0);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setsigmask(&attr, &set), 0);

  short flags = -1;
  pid_t group = -1;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getflags(&other, &flags), 0);
  EXPECT_EQ(flags, short(0));
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getpgroup(&other, &group), 0);
  EXPECT_EQ(group, pid_t(0));
  param.sched_priority = 0;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setschedparam(&other, &param), 0);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setschedpolicy(&other, SCHED_OTHER),
            0);
  sigset_t empty = {};
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setsigdefault(&other, &empty), 0);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_setsigmask(&other, &empty), 0);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_destroy(&other), 0);

  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getflags(&attr, &flags), 0);
  EXPECT_EQ(flags, ALL_FLAGS);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getpgroup(&attr, &group), 0);
  EXPECT_EQ(group, pid_t(17));
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getschedparam(&attr, &param), 0);
  EXPECT_EQ(param.sched_priority, 23);
  int policy = -1;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnattr_getschedpolicy(&attr, &policy), 0);
  EXPECT_EQ(policy, SCHED_FIFO);
  checkSignals(set, set);
}
