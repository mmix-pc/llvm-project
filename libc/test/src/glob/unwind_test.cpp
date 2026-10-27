//===-- Glob resource ownership during provider unwind --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/glob_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/File/dir.h"
#include "src/glob/globfree.h"
#include "src/sys/stat/lstat.h"
#include "src/sys/stat/stat.h"
#include "test/UnitTest/Test.h"
#include <pthread.h>
#include <stdlib.h>

namespace {
void *allocations[64];
size_t live;
bool track;
void forget(void *pointer) {
  for (size_t i = 0; i != live; ++i)
    if (allocations[i] == pointer) {
      allocations[i] = allocations[--live];
      return;
    }
}
} // namespace

extern "C" void *__real_realloc(void *, size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_realloc(void *old, size_t size) {
  void *next = __real_realloc(old, size);
  if (track && next) {
    forget(old);
    if (live == 64)
      abort();
    allocations[live++] = next;
  }
  return next;
}
extern "C" void __wrap_free(void *pointer) {
  if (track)
    forget(pointer);
  __real_free(pointer);
}

namespace LIBC_NAMESPACE_DECL {
namespace {
struct ReadFailure {};
unsigned reads, closes;
bool cancel_read;
class UnwindDirectory {
public:
  static ErrorOr<UnwindDirectory *> open(const char *) {
    static UnwindDirectory directory;
    reads = 0;
    return &directory;
  }
  ErrorOr<struct dirent *> read() {
    static struct dirent entry = {};
    if (reads++) {
      if (cancel_read) {
        if (::pthread_cancel(::pthread_self()) != 0)
          abort();
        ::pthread_testcancel();
        abort();
      }
      throw ReadFailure{};
    }
    entry.d_name[0] = 'a';
    entry.d_name[1] = '\0';
    return &entry;
  }
  int close(bool) {
    ++closes;
    return 0;
  }
};
int test_stat(const char *, struct stat *result) {
  *result = {};
  result->st_mode = S_IFREG | 0600;
  return 0;
}
} // namespace
} // namespace LIBC_NAMESPACE_DECL

// Exercise the production walker and transaction, substituting only leaves.
#define Dir UnwindDirectory
#define glob unwind_glob
#define lstat test_stat
#define stat(...) test_stat(__VA_ARGS__)
#include "src/glob/glob.cpp"
#undef stat
#undef lstat
#undef Dir
#undef glob

namespace {
struct Input {
  glob_t result = {};
  int flags = GLOB_DOOFFS;
};
void *run(void *arg) {
  auto &input = *static_cast<Input *>(arg);
  LIBC_NAMESPACE::unwind_glob("dir/*", input.flags, nullptr, &input.result);
  return nullptr;
}
} // namespace

TEST(LlvmLibcGlobUnwindTest, ReleasesNewResultsAndPreservesAppendOwnership) {
  using namespace LIBC_NAMESPACE;
  for (int cancel = 0; cancel != 2; ++cancel) {
    for (int append = 0; append != 2; ++append) {
      Input input;
      input.result.gl_offs = 2;
      track = true;
      if (append) {
        ASSERT_EQ(LIBC_NAMESPACE::unwind_glob("old", GLOB_DOOFFS, nullptr,
                                      &input.result), 0);
        input.flags |= GLOB_APPEND;
      }
      const size_t prior_live = live;
      char *prior = append ? input.result.gl_pathv[2] : nullptr;
      closes = reads = 0;
      cancel_read = cancel;
      if (cancel) {
        pthread_t thread;
        ASSERT_EQ(::pthread_create(&thread, nullptr, run, &input), 0);
        void *status = nullptr;
        ASSERT_EQ(::pthread_join(thread, &status), 0);
        EXPECT_EQ(status, PTHREAD_CANCELED);
      } else {
        bool caught = false;
        try {
          run(&input);
        } catch (ReadFailure) {
          caught = true;
        }
        EXPECT_TRUE(caught);
      }
      EXPECT_EQ(reads, 2U);
      EXPECT_EQ(closes, 1U);
      EXPECT_EQ(live, prior_live);
      EXPECT_EQ(input.result.gl_pathc, size_t(append));
      if (append) {
        EXPECT_EQ(input.result.gl_pathv[0], static_cast<char *>(nullptr));
        EXPECT_EQ(input.result.gl_pathv[1], static_cast<char *>(nullptr));
        EXPECT_EQ(input.result.gl_pathv[2], prior);
        EXPECT_STREQ(prior, "old");
        EXPECT_EQ(input.result.gl_pathv[3], static_cast<char *>(nullptr));
      }
      LIBC_NAMESPACE::globfree(&input.result);
      EXPECT_EQ(live, size_t(0));
      track = false;
    }
  }
}
