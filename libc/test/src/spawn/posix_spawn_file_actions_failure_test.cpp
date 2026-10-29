//===-- Spawn file action ownership, rollback and range tests -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/limits_macros.h"
#include "hdr/unistd_macros.h"
#include "src/__support/libc_errno.h"
#include "src/spawn/file_actions.h"
#include "src/spawn/posix_spawn_file_actions_addchdir.h"
#include "src/spawn/posix_spawn_file_actions_addclose.h"
#include "src/spawn/posix_spawn_file_actions_adddup2.h"
#include "src/spawn/posix_spawn_file_actions_addfchdir.h"
#include "src/spawn/posix_spawn_file_actions_addopen.h"
#include "src/spawn/posix_spawn_file_actions_destroy.h"
#include "src/spawn/posix_spawn_file_actions_init.h"
#include "test/UnitTest/Test.h"

#if !defined(LIBC_FULL_BUILD) ||                                               \
    !defined(LLVM_LIBC_TYPES_POSIX_SPAWN_FILE_ACTIONS_T_H)
#error "This test requires the full-build LLVM spawn file action type"
#endif

namespace {
long open_max = 64;
int query_error, query_count, query_name;
bool tracking;
unsigned allocation_count, fail_at, live_allocations;
void *allocations[16];
posix_spawn_file_actions_t *observed_actions;
void *expected_front, *expected_back;
bool published_during_allocation;

void track_allocations(unsigned failure = 0) {
  allocation_count = 0;
  fail_at = failure;
  tracking = true;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
long sysconf(int name) {
  ++query_count;
  query_name = name;
  if (query_error) {
    libc_errno = query_error;
    return -1;
  }
  return open_max;
}
} // namespace LIBC_NAMESPACE_DECL

extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);

extern "C" void *__wrap_malloc(size_t size) {
  if (observed_actions &&
      (observed_actions->__front != expected_front ||
       observed_actions->__back != expected_back ||
       (expected_back &&
        static_cast<LIBC_NAMESPACE::BaseSpawnFileAction *>(expected_back)
            ->next)))
    published_during_allocation = true;
  if (tracking && ++allocation_count == fail_at)
    return nullptr;
  void *result = __real_malloc(size);
  if (tracking && result) {
    ++live_allocations;
    for (void *&allocation : allocations) {
      if (!allocation) {
        allocation = result;
        break;
      }
    }
  }
  return result;
}

extern "C" void __wrap_free(void *ptr) {
  if (ptr) {
    for (void *&allocation : allocations) {
      if (allocation == ptr) {
        allocation = nullptr;
        --live_allocations;
        break;
      }
    }
  }
  __real_free(ptr);
}

TEST(LlvmLibcSpawnFileActionsFailureTest, DestroyReleasesOwnedPaths) {
  open_max = 64;
  query_error = 0;
  posix_spawn_file_actions_t actions;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
  char path[] = "original";
  track_allocations();
  int first =
      LIBC_NAMESPACE::posix_spawn_file_actions_addopen(&actions, 0, path, 0, 0);
  int second =
      LIBC_NAMESPACE::posix_spawn_file_actions_addopen(&actions, 1, path, 0, 0);
  int close = LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions, 2);
  int dup = LIBC_NAMESPACE::posix_spawn_file_actions_adddup2(&actions, 3, 4);
  tracking = false;
  ASSERT_EQ(first, 0);
  ASSERT_EQ(second, 0);
  ASSERT_EQ(close, 0);
  ASSERT_EQ(dup, 0);
  EXPECT_EQ(live_allocations, 6U);
  auto *a = static_cast<LIBC_NAMESPACE::SpawnFileOpenAction *>(actions.__front);
  auto *b = static_cast<LIBC_NAMESPACE::SpawnFileOpenAction *>(a->next);
  EXPECT_FALSE(a->path == path);
  EXPECT_FALSE(b->path == path);
  EXPECT_NE(a->path, b->path);
  path[0] = 'X';
  EXPECT_STREQ(a->path, "original");
  EXPECT_STREQ(b->path, "original");
  EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
  EXPECT_EQ(live_allocations, 0U);
  EXPECT_EQ(actions.__front, nullptr);
  EXPECT_EQ(actions.__back, nullptr);
  EXPECT_STREQ(path, "Xriginal");
}

TEST(LlvmLibcSpawnFileActionsFailureTest, AllocationFailurePreservesList) {
  open_max = 64;
  query_error = 0;
  const bool modes[] = {false, true};
  for (bool populated : modes) {
    for (unsigned failure = 1; failure <= 2; ++failure) {
      posix_spawn_file_actions_t actions;
      ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
      if (populated) {
        track_allocations();
        int result = LIBC_NAMESPACE::posix_spawn_file_actions_addopen(
            &actions, 0, "retained", 0, 0);
        tracking = false;
        ASSERT_EQ(result, 0);
        ASSERT_EQ(
            LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions, 1), 0);
      }
      void *front = actions.__front;
      void *back = actions.__back;
      unsigned live_before = live_allocations;
      char path[] = "rejected";
      expected_front = front;
      expected_back = back;
      published_during_allocation = false;
      observed_actions = &actions;
      track_allocations(failure);
      int result = LIBC_NAMESPACE::posix_spawn_file_actions_addopen(&actions, 2,
                                                                    path, 0, 0);
      tracking = false;
      observed_actions = nullptr;
      EXPECT_EQ(result, ENOMEM);
      EXPECT_FALSE(published_during_allocation);
      EXPECT_EQ(allocation_count, failure);
      EXPECT_EQ(live_allocations, live_before);
      EXPECT_EQ(actions.__front, front);
      EXPECT_EQ(actions.__back, back);
      EXPECT_STREQ(path, "rejected");
      if (populated) {
        auto *old = static_cast<LIBC_NAMESPACE::SpawnFileOpenAction *>(front);
        EXPECT_STREQ(old->path, "retained");
        EXPECT_EQ(static_cast<void *>(old->next), back);
        EXPECT_EQ(
            static_cast<LIBC_NAMESPACE::BaseSpawnFileAction *>(back)->next,
            nullptr);
      }
      observed_actions = &actions;
      track_allocations();
      result = LIBC_NAMESPACE::posix_spawn_file_actions_addopen(&actions, 2,
                                                                path, 0, 0);
      tracking = false;
      observed_actions = nullptr;
      EXPECT_EQ(result, 0);
      EXPECT_FALSE(published_during_allocation);
      EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
      EXPECT_EQ(live_allocations, 0U);
    }
  }
}

TEST(LlvmLibcSpawnFileActionsFailureTest, Dup2AllocationFailurePreservesList) {
  open_max = 64;
  query_error = 0;
  posix_spawn_file_actions_t actions;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions, 0), 0);
  void *front = actions.__front;
  track_allocations(1);
  int result = LIBC_NAMESPACE::posix_spawn_file_actions_adddup2(&actions, 1, 2);
  tracking = false;
  EXPECT_EQ(result, ENOMEM);
  EXPECT_EQ(allocation_count, 1U);
  EXPECT_EQ(live_allocations, 0U);
  EXPECT_EQ(actions.__front, front);
  EXPECT_EQ(actions.__back, front);
  EXPECT_EQ(static_cast<LIBC_NAMESPACE::BaseSpawnFileAction *>(front)->next,
            nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_adddup2(&actions, 1, 2),
            0);
  EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
}

TEST(LlvmLibcSpawnFileActionsFailureTest, DescriptorRangePreservesList) {
  query_error = 0;
  posix_spawn_file_actions_t actions;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
  ASSERT_EQ(
      LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions, INT_MAX), 0);
  void *front = actions.__front;
  const long limits[] = {1, 17, 64};
  for (long limit : limits) {
    open_max = limit;
    const int invalid[] = {-1, INT_MIN, int(limit), int(limit) + 1, INT_MAX};
    for (int fd : invalid) {
      query_count = 0;
      libc_errno = EDOM;
      track_allocations(1);
      // No pathname access or allocation is allowed for an invalid descriptor.
      int open = LIBC_NAMESPACE::posix_spawn_file_actions_addopen(
          &actions, fd, nullptr, 0, 0);
      int source =
          LIBC_NAMESPACE::posix_spawn_file_actions_adddup2(&actions, fd, 0);
      int target =
          LIBC_NAMESPACE::posix_spawn_file_actions_adddup2(&actions, 0, fd);
      tracking = false;
      EXPECT_EQ(open, EBADF);
      EXPECT_EQ(source, EBADF);
      EXPECT_EQ(target, EBADF);
      EXPECT_EQ(int(libc_errno), EDOM);
      EXPECT_EQ(allocation_count, 0U);
      EXPECT_EQ(query_count, fd < 0 ? 0 : 3);
      EXPECT_EQ(actions.__front, front);
      EXPECT_EQ(actions.__back, front);
      EXPECT_EQ(static_cast<LIBC_NAMESPACE::BaseSpawnFileAction *>(front)->next,
                nullptr);
    }
  }
  EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
}

TEST(LlvmLibcSpawnFileActionsFailureTest, DynamicAndIndeterminateLimits) {
  query_error = 0;
  posix_spawn_file_actions_t actions;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
  const long limits[] = {1, 17, 64, LONG_MAX, -1};
  for (long limit : limits) {
    open_max = limit;
    int fd = limit == -1 || limit > INT_MAX ? INT_MAX : int(limit) - 1;
    query_count = 0;
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_addopen(
                  &actions, fd, "not/opened/by/registration", 0, 0),
              0);
    EXPECT_EQ(
        LIBC_NAMESPACE::posix_spawn_file_actions_adddup2(&actions, fd, fd), 0);
    EXPECT_EQ(query_count, 2);
    EXPECT_EQ(query_name, int(_SC_OPEN_MAX));
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  open_max = 0;
  EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_addopen(&actions, 0,
                                                             nullptr, 0, 0),
            EBADF);
  EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_adddup2(&actions, 0, 0),
            EBADF);
  query_count = 0;
  EXPECT_EQ(
      LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions, INT_MAX), 0);
  EXPECT_EQ(query_count, 0);
  EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
}

TEST(LlvmLibcSpawnFileActionsFailureTest, QueryFailurePreservesListAndErrno) {
  open_max = 64;
  query_error = EIO;
  posix_spawn_file_actions_t actions;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions, 0), 0);
  void *front = actions.__front;
  libc_errno = EDOM;
  track_allocations(1);
  int open = LIBC_NAMESPACE::posix_spawn_file_actions_addopen(&actions, 0,
                                                              nullptr, 0, 0);
  int dup = LIBC_NAMESPACE::posix_spawn_file_actions_adddup2(&actions, 0, 0);
  tracking = false;
  EXPECT_EQ(open, EIO);
  EXPECT_EQ(dup, EIO);
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(allocation_count, 0U);
  EXPECT_EQ(actions.__front, front);
  EXPECT_EQ(actions.__back, front);
  EXPECT_EQ(static_cast<LIBC_NAMESPACE::BaseSpawnFileAction *>(front)->next,
            nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
  query_error = 0;
}

TEST(LlvmLibcSpawnFileActionsFailureTest, MixedCwdActionsOwnPathsInOrder) {
  open_max = 64;
  query_error = 0;
  query_count = 0;
  posix_spawn_file_actions_t actions;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
  char path[] = "missing/../directory";
  track_allocations();
  int first = LIBC_NAMESPACE::posix_spawn_file_actions_addchdir(&actions, path);
  int open = LIBC_NAMESPACE::posix_spawn_file_actions_addopen(&actions, 1,
                                                              "relative", 0, 0);
  int dup = LIBC_NAMESPACE::posix_spawn_file_actions_adddup2(&actions, 1, 2);
  int fchdir = LIBC_NAMESPACE::posix_spawn_file_actions_addfchdir(&actions, 2);
  int close = LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions, 2);
  int last = LIBC_NAMESPACE::posix_spawn_file_actions_addchdir(&actions, path);
  tracking = false;
  ASSERT_EQ(first, 0);
  ASSERT_EQ(open, 0);
  ASSERT_EQ(dup, 0);
  ASSERT_EQ(fchdir, 0);
  ASSERT_EQ(close, 0);
  ASSERT_EQ(last, 0);
  EXPECT_EQ(live_allocations, 9U);
  EXPECT_EQ(query_count, 2);

  using Base = LIBC_NAMESPACE::BaseSpawnFileAction;
  auto *a = static_cast<Base *>(actions.__front);
  ASSERT_NE(a, nullptr);
  ASSERT_EQ(a->type, Base::CHDIR);
  auto *b = a->next;
  ASSERT_NE(b, nullptr);
  ASSERT_EQ(b->type, Base::OPEN);
  EXPECT_STREQ(static_cast<LIBC_NAMESPACE::SpawnFileOpenAction *>(b)->path,
               "relative");
  auto *c = b->next;
  ASSERT_NE(c, nullptr);
  ASSERT_EQ(c->type, Base::DUP2);
  auto *d = c->next;
  ASSERT_NE(d, nullptr);
  ASSERT_EQ(d->type, Base::FCHDIR);
  EXPECT_EQ(static_cast<LIBC_NAMESPACE::SpawnFileFchdirAction *>(d)->fd, 2);
  auto *e = d->next;
  ASSERT_NE(e, nullptr);
  ASSERT_EQ(e->type, Base::CLOSE);
  auto *f = e->next;
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(f->type, Base::CHDIR);
  EXPECT_EQ(static_cast<void *>(f), actions.__back);
  EXPECT_EQ(f->next, nullptr);

  auto *first_chdir = static_cast<LIBC_NAMESPACE::SpawnFileChdirAction *>(a);
  auto *last_chdir = static_cast<LIBC_NAMESPACE::SpawnFileChdirAction *>(f);
  EXPECT_FALSE(first_chdir->path == path);
  EXPECT_FALSE(last_chdir->path == path);
  EXPECT_NE(first_chdir->path, last_chdir->path);
  path[0] = 'X';
  EXPECT_STREQ(first_chdir->path, "missing/../directory");
  EXPECT_STREQ(last_chdir->path, "missing/../directory");
  EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
  EXPECT_EQ(live_allocations, 0U);
  EXPECT_EQ(actions.__front, nullptr);
  EXPECT_EQ(actions.__back, nullptr);
  EXPECT_STREQ(path, "Xissing/../directory");
}

TEST(LlvmLibcSpawnFileActionsFailureTest, ChdirAllocationFailurePreservesList) {
  const bool modes[] = {false, true};
  for (bool populated : modes) {
    for (unsigned failure = 1; failure <= 2; ++failure) {
      posix_spawn_file_actions_t actions;
      ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
      if (populated) {
        track_allocations();
        int chdir = LIBC_NAMESPACE::posix_spawn_file_actions_addchdir(
            &actions, "retained");
        int fchdir =
            LIBC_NAMESPACE::posix_spawn_file_actions_addfchdir(&actions, 0);
        tracking = false;
        ASSERT_EQ(chdir, 0);
        ASSERT_EQ(fchdir, 0);
      }
      void *front = actions.__front;
      void *back = actions.__back;
      unsigned live_before = live_allocations;
      char path[] = "rejected";
      expected_front = front;
      expected_back = back;
      published_during_allocation = false;
      observed_actions = &actions;
      track_allocations(failure);
      int result =
          LIBC_NAMESPACE::posix_spawn_file_actions_addchdir(&actions, path);
      tracking = false;
      observed_actions = nullptr;
      EXPECT_EQ(result, ENOMEM);
      EXPECT_FALSE(published_during_allocation);
      EXPECT_EQ(allocation_count, failure);
      EXPECT_EQ(live_allocations, live_before);
      EXPECT_EQ(actions.__front, front);
      EXPECT_EQ(actions.__back, back);
      EXPECT_STREQ(path, "rejected");
      if (populated) {
        auto *old = static_cast<LIBC_NAMESPACE::SpawnFileChdirAction *>(front);
        EXPECT_STREQ(old->path, "retained");
        EXPECT_EQ(static_cast<void *>(old->next), back);
        EXPECT_EQ(
            static_cast<LIBC_NAMESPACE::BaseSpawnFileAction *>(back)->next,
            nullptr);
      }
      observed_actions = &actions;
      track_allocations();
      result =
          LIBC_NAMESPACE::posix_spawn_file_actions_addchdir(&actions, path);
      tracking = false;
      observed_actions = nullptr;
      EXPECT_EQ(result, 0);
      EXPECT_FALSE(published_during_allocation);
      EXPECT_EQ(live_allocations, live_before + 2);
      EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
      EXPECT_EQ(live_allocations, 0U);
    }
  }
}

TEST(LlvmLibcSpawnFileActionsFailureTest,
     FchdirAllocationFailurePreservesList) {
  const bool modes[] = {false, true};
  for (bool populated : modes) {
    posix_spawn_file_actions_t actions;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
    if (populated) {
      track_allocations();
      int result = LIBC_NAMESPACE::posix_spawn_file_actions_addchdir(
          &actions, "retained");
      tracking = false;
      ASSERT_EQ(result, 0);
    }
    void *front = actions.__front;
    void *back = actions.__back;
    unsigned live_before = live_allocations;
    expected_front = front;
    expected_back = back;
    published_during_allocation = false;
    observed_actions = &actions;
    track_allocations(1);
    int result =
        LIBC_NAMESPACE::posix_spawn_file_actions_addfchdir(&actions, 0);
    tracking = false;
    observed_actions = nullptr;
    EXPECT_EQ(result, ENOMEM);
    EXPECT_FALSE(published_during_allocation);
    EXPECT_EQ(allocation_count, 1U);
    EXPECT_EQ(live_allocations, live_before);
    EXPECT_EQ(actions.__front, front);
    EXPECT_EQ(actions.__back, back);
    if (populated) {
      auto *old = static_cast<LIBC_NAMESPACE::SpawnFileChdirAction *>(front);
      EXPECT_STREQ(old->path, "retained");
      EXPECT_EQ(old->next, nullptr);
    }
    observed_actions = &actions;
    track_allocations();
    result = LIBC_NAMESPACE::posix_spawn_file_actions_addfchdir(&actions, 0);
    tracking = false;
    observed_actions = nullptr;
    EXPECT_EQ(result, 0);
    EXPECT_FALSE(published_during_allocation);
    EXPECT_EQ(live_allocations, live_before + 1);
    EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
    EXPECT_EQ(live_allocations, 0U);
  }
}

TEST(LlvmLibcSpawnFileActionsFailureTest,
     CwdRegistrationDefersOperationErrors) {
  // fchdir has no OPEN_MAX check, even if the limit query would fail.
  open_max = 64;
  query_error = EIO;
  query_count = 0;
  posix_spawn_file_actions_t actions;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_addchdir(&actions, ""), 0);
  auto *empty =
      static_cast<LIBC_NAMESPACE::SpawnFileChdirAction *>(actions.__front);
  EXPECT_STREQ(empty->path, "");
  const int descriptors[] = {0, 63, 64, INT_MAX};
  for (int fd : descriptors) {
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_addfchdir(&actions, fd),
              0);
    auto *act =
        static_cast<LIBC_NAMESPACE::SpawnFileFchdirAction *>(actions.__back);
    EXPECT_EQ(act->type, LIBC_NAMESPACE::BaseSpawnFileAction::FCHDIR);
    EXPECT_EQ(act->fd, fd);
    EXPECT_EQ(act->next, nullptr);
  }
  EXPECT_EQ(query_count, 0);
  EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
  query_error = 0;
}

TEST(LlvmLibcSpawnFileActionsFailureTest, InvalidCwdRegistrationPreservesList) {
  query_count = 0;
  track_allocations(1);
  int chdir =
      LIBC_NAMESPACE::posix_spawn_file_actions_addchdir(nullptr, "unused");
  int fchdir = LIBC_NAMESPACE::posix_spawn_file_actions_addfchdir(nullptr, 0);
  tracking = false;
  EXPECT_EQ(chdir, EINVAL);
  EXPECT_EQ(fchdir, EINVAL);
  EXPECT_EQ(allocation_count, 0U);

  const bool modes[] = {false, true};
  for (bool populated : modes) {
    posix_spawn_file_actions_t actions;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
    if (populated)
      ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_addchdir(&actions,
                                                                  "retained"),
                0);
    void *front = actions.__front;
    void *back = actions.__back;
    const int invalid[] = {-1, INT_MIN};
    for (int fd : invalid) {
      track_allocations(1);
      int result =
          LIBC_NAMESPACE::posix_spawn_file_actions_addfchdir(&actions, fd);
      tracking = false;
      EXPECT_EQ(result, EBADF);
      EXPECT_EQ(allocation_count, 0U);
      EXPECT_EQ(live_allocations, 0U);
      EXPECT_EQ(actions.__front, front);
      EXPECT_EQ(actions.__back, back);
      if (populated) {
        auto *old = static_cast<LIBC_NAMESPACE::SpawnFileChdirAction *>(front);
        EXPECT_STREQ(old->path, "retained");
        EXPECT_EQ(old->next, nullptr);
      }
    }
    EXPECT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
  }
  EXPECT_EQ(query_count, 0);
}
