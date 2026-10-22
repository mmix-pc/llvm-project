//===-- Callback cancellation ownership for nftw --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Separate pthread-enabled executable with the real adapter/engine. Compile
// this source and all three adapter objects with -fexceptions -funwind-tables.
// The public pthread
// calls bind to the selected runtime: host pthread results are host evidence,
// not qualification of target cancellation or arbitrary async cancellation.

#include "hdr/fcntl_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/fcntl/open.h"
#include "src/ftw/nftw.h"
#include "src/stdio/snprintf.h"
#include "src/stdlib/mkdtemp.h"
#include "src/sys/stat/stat.h"
#include "src/unistd/close.h"
#include "src/unistd/rmdir.h"
#include "src/unistd/unlink.h"
#include "test/UnitTest/Test.h"
#include <ftw.h>
#include <pthread.h>

#ifndef __cpp_exceptions
#error "nftw cancellation cleanup tests require exceptions enabled"
#endif

namespace {
struct CancellationState {
  char root[64] = "nftw-cancel-XXXXXX";
  char file[128];
  int callbacks = 0;
  int pthread_error = 0;
  int cleanup_stat_result = -1;
  bool callback_cleanup = false;
  bool outer_cleanup = false;
  bool returned = false;
  bool callback_errno_was_zero = false;
  struct stat original{};
  struct stat inside{};
  struct stat restored{};
} state;

struct CallbackOwner {
  ~CallbackOwner() { state.callback_cleanup = true; }
};

struct OuterOwner {
  ~OuterOwner() {
    state.outer_cleanup = true;
    state.cleanup_stat_result = LIBC_NAMESPACE::stat(".", &state.restored);
  }
};

int cancel_callback(const char *, const struct stat *, int, FTW *position) {
  ++state.callbacks;
  if (!position->level)
    return 0;
  state.callback_errno_was_zero = int(LIBC_NAMESPACE::libc_errno) == 0;
  if (LIBC_NAMESPACE::stat(".", &state.inside))
    return 43;
  CallbackOwner owner;
  state.pthread_error = ::pthread_cancel(::pthread_self());
  if (state.pthread_error)
    return 44;
  ::pthread_testcancel();
  state.returned = true;
  return 45;
}

void *worker(void *) {
  OuterOwner owner;
  state.pthread_error =
      ::pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, nullptr);
  if (state.pthread_error)
    return nullptr;
  state.pthread_error =
      ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
  if (state.pthread_error)
    return nullptr;
  LIBC_NAMESPACE::nftw(state.root, cancel_callback, 1, FTW_CHDIR | FTW_PHYS);
  state.returned = true;
  return nullptr;
}

class LlvmLibcNftwCancelTest : public LIBC_NAMESPACE::testing::Test {
protected:
  bool descriptors[256]{};

  void SetUp() override {
    state = {};
    ASSERT_NE(LIBC_NAMESPACE::mkdtemp(state.root), nullptr);
    LIBC_NAMESPACE::snprintf(state.file, sizeof(state.file), "%s/leaf",
                             state.root);
    int fd = LIBC_NAMESPACE::open(
        state.file, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(LIBC_NAMESPACE::close(fd), 0);
    ASSERT_EQ(LIBC_NAMESPACE::stat(".", &state.original), 0);
    for (int i = 0; i < 256; ++i)
      descriptors[i] = LIBC_NAMESPACE::fcntl(i, F_GETFD) >= 0;
  }

  void TearDown() override {
    EXPECT_EQ(LIBC_NAMESPACE::unlink(state.file), 0);
    EXPECT_EQ(LIBC_NAMESPACE::rmdir(state.root), 0);
    LIBC_NAMESPACE::libc_errno = 0;
  }
};
} // namespace

TEST_F(LlvmLibcNftwCancelTest,
       DeferredCallbackUnwindRestoresCwdAndClosesAnchor) {
  pthread_t thread;
  ASSERT_EQ(::pthread_create(&thread, nullptr, worker, nullptr), 0);
  void *result = nullptr;
  ASSERT_EQ(::pthread_join(thread, &result), 0);
  EXPECT_EQ(result, PTHREAD_CANCELED);
  EXPECT_EQ(state.pthread_error, 0);
  EXPECT_EQ(state.callbacks, 2);
  EXPECT_TRUE(state.callback_errno_was_zero);
  EXPECT_TRUE(state.callback_cleanup);
  EXPECT_TRUE(state.outer_cleanup);
  EXPECT_FALSE(state.returned);
  EXPECT_EQ(state.cleanup_stat_result, 0);
  EXPECT_EQ(state.restored.st_dev, state.original.st_dev);
  EXPECT_EQ(state.restored.st_ino, state.original.st_ino);
  EXPECT_TRUE(state.inside.st_dev != state.original.st_dev ||
              state.inside.st_ino != state.original.st_ino);
  for (int fd = 0; fd < 256; ++fd)
    EXPECT_EQ(LIBC_NAMESPACE::fcntl(fd, F_GETFD) >= 0, descriptors[fd]);
}
