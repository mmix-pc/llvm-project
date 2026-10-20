//===-- Directory scan failure and unwind ownership tests -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/dirent/scandir_utils.h"
#include "src/dirent/readdir_r.h"
#include "test/UnitTest/Test.h"
#include <pthread.h>
#include <string.h>

static unsigned opened, closed, reads, fail_read, allocations, fail_allocation;
static void *live[128];
static size_t live_count;
static bool track;
static unsigned comparisons;
static unsigned malformed;

extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *, size_t);
extern "C" void __real_free(void *);
static void forget(void *pointer) {
  for (size_t i = 0; i < live_count; ++i)
    if (live[i] == pointer) {
      live[i] = live[--live_count];
      return;
    }
}
extern "C" void *__wrap_malloc(size_t size) {
  if (track && ++allocations == fail_allocation)
    return nullptr;
  void *result = __real_malloc(size);
  if (track && result)
    live[live_count++] = result;
  return result;
}
extern "C" void *__wrap_realloc(void *pointer, size_t size) {
  if (track && ++allocations == fail_allocation)
    return nullptr;
  void *result = __real_realloc(pointer, size);
  if (track && result) {
    forget(pointer);
    live[live_count++] = result;
  }
  return result;
}
extern "C" void __wrap_free(void *pointer) {
  if (track)
    forget(pointer);
  __real_free(pointer);
}

namespace LIBC_NAMESPACE_DECL {
int openat(int, const char *, int, ...) {
  ++opened;
  return 7;
}
ErrorOr<int> platform_opendir(const char *) { return 7; }
int platform_closedir(int fd) {
  EXPECT_EQ(fd, 7);
  ++closed;
  return 0;
}
ErrorOr<off_t> platform_seekdir(int, off_t, int) { return 0; }
off_t platform_dir_offset(const struct dirent *d) { return d->d_off; }
size_t platform_dir_reclen(struct dirent *d) { return d->d_reclen; }
ErrorOr<size_t> platform_fetch_dirents(int, cpp::span<uint8_t> buffer) {
  ++reads;
  if (reads == fail_read)
    return Error(EIO);
  if (reads > 20)
    return size_t(0);
  memset(buffer.data(), 0, 32);
  auto *entry = reinterpret_cast<dirent *>(buffer.data());
  entry->d_reclen = 32;
  entry->d_off = reads;
  entry->d_ino = reads;
  entry->d_type = DT_REG;
  entry->d_name[0] = static_cast<char>('z' - reads);
  if (malformed == 1)
    entry->d_reclen = 1;
  if (malformed == 2)
    entry->d_reclen = 33;
  if (malformed == 3)
    memset(entry->d_name, 'x', 32 - offsetof(dirent, d_name));
  if (malformed == 4)
    return buffer.size() + 1;
  return size_t(32);
}
} // namespace LIBC_NAMESPACE_DECL

static void reset(unsigned failure = 0) {
  opened = closed = reads = fail_read = allocations = 0;
  fail_allocation = failure;
  live_count = 0;
  comparisons = 0;
  malformed = 0;
  track = true;
}
static void check_released() {
  track = false;
  EXPECT_EQ(opened, 1U);
  EXPECT_EQ(closed, 1U);
  EXPECT_EQ(live_count, size_t(0));
}
static int throwing_filter(const dirent *) {
  if (reads == 3)
    throw 17;
  return 1;
}
static int throwing_compare(const dirent **left, const dirent **right) {
  if (++comparisons == 15)
    throw 18;
  return strcmp((*left)->d_name, (*right)->d_name);
}
static int cancelling_filter(const dirent *) {
  if (reads == 3)
    pthread_testcancel();
  return 1;
}

TEST(LlvmLibcScandirOwnership, AllocationFailures) {
  // Dir, initial vector, twenty entries, and one vector growth.
  for (unsigned failure = 1; failure <= 23; ++failure) {
    reset(failure);
    dirent *sentinel = nullptr;
    dirent **list = &sentinel;
    EXPECT_EQ(LIBC_NAMESPACE::internal::scan_directory(0, "unused", &list,
                                                       nullptr, nullptr),
              -1);
    EXPECT_EQ(list, &sentinel);
    EXPECT_EQ(int(libc_errno), ENOMEM);
    check_released();
  }
  libc_errno = 0;
}

TEST(LlvmLibcScandirOwnership, ReadFailure) {
  reset();
  fail_read = 3;
  dirent **list = nullptr;
  EXPECT_EQ(LIBC_NAMESPACE::internal::scan_directory(0, "unused", &list,
                                                     nullptr, nullptr),
            -1);
  EXPECT_EQ(list, static_cast<dirent **>(nullptr));
  EXPECT_EQ(int(libc_errno), EIO);
  check_released();
  libc_errno = 0;
}

TEST(LlvmLibcScandirOwnership, InvalidRecordsDoNotEscape) {
  for (unsigned kind = 1; kind <= 4; ++kind) {
    reset();
    malformed = kind;
    dirent **list = nullptr;
    EXPECT_EQ(LIBC_NAMESPACE::internal::scan_directory(
                  0, "unused", &list, nullptr, nullptr),
              -1);
    EXPECT_EQ(list, static_cast<dirent **>(nullptr));
    EXPECT_EQ(int(libc_errno), EIO);
    check_released();
  }
  libc_errno = 0;
}

TEST(LlvmLibcScandirOwnership, ReentrantReadReturnsErrorWithoutChangingErrno) {
  reset();
  fail_read = 1;
  auto dir = LIBC_NAMESPACE::Dir::fdopen(7);
  ASSERT_TRUE(dir.has_value());
  alignas(dirent) unsigned char storage[sizeof(dirent) + 256];
  auto *entry = reinterpret_cast<dirent *>(storage);
  dirent *result = entry;
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::readdir_r(reinterpret_cast<DIR *>(dir.value()),
                                     entry, &result),
            EIO);
  EXPECT_EQ(result, static_cast<dirent *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(dir.value()->close(), 0);
  EXPECT_EQ(live_count, size_t(0));
  track = false;
  libc_errno = 0;
}

TEST(LlvmLibcScandirOwnership, CallbackExceptions) {
  for (int kind = 0; kind != 2; ++kind) {
    reset();
    dirent **list = nullptr;
    try {
      LIBC_NAMESPACE::internal::scan_directory(
          0, "unused", &list, kind == 0 ? throwing_filter : nullptr,
          kind == 1 ? throwing_compare : nullptr);
      EXPECT_TRUE(false);
    } catch (int value) {
      EXPECT_EQ(value, kind + 17);
    }
    EXPECT_EQ(list, static_cast<dirent **>(nullptr));
    check_released();
  }
}

static void *cancel_scan(void *) {
  pthread_cancel(pthread_self());
  dirent **list = nullptr;
  LIBC_NAMESPACE::internal::scan_directory(0, "unused", &list,
                                           cancelling_filter, nullptr);
  return nullptr;
}

TEST(LlvmLibcScandirOwnership, Cancellation) {
  reset();
  pthread_t thread;
  ASSERT_EQ(pthread_create(&thread, nullptr, cancel_scan, nullptr), 0);
  void *result = nullptr;
  ASSERT_EQ(pthread_join(thread, &result), 0);
  EXPECT_EQ(result, PTHREAD_CANCELED);
  check_released();
}
