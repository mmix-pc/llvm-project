//===-- Tests for LinuxFile allocation failure cleanup --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/File/linux/file.h"
#include "test/UnitTest/Test.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

namespace {
bool tracking;
unsigned allocation_count, fail_at, live_allocations;
void *allocations[4];
LIBC_NAMESPACE::File *closing_file;
bool published_at_reclamation;
unsigned reclamation_checks;

void start_tracking(unsigned failure) {
  allocation_count = live_allocations = 0;
  fail_at = failure;
  for (void *&allocation : allocations)
    allocation = nullptr;
  tracking = true;
}

int next_descriptor() {
  int fd = ::open("/dev/null", O_RDONLY);
  if (fd >= 0)
    ::close(fd);
  return fd;
}
} // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);

// Intercept only allocation, leaving the actual LinuxFile and kernel I/O paths
// intact. Assertions run with failure injection disabled.
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++allocation_count == fail_at)
    return nullptr;
  void *result = __real_malloc(size);
  if (tracking && result) {
    for (void *&allocation : allocations) {
      if (!allocation) {
        allocation = result;
        ++live_allocations;
        break;
      }
    }
  }
  return result;
}

extern "C" void __wrap_free(void *ptr) {
  if (ptr && closing_file) {
    // Inspect at the exact reclamation boundary, not after close returns.
    // This also follows the lock order used by fflush(NULL).
    LIBC_NAMESPACE::File::lock_list();
    for (auto *f = LIBC_NAMESPACE::File::get_first_file(); f;
         f = f->get_next()) {
      if (f == closing_file)
        published_at_reclamation = true;
      else
        f->flush();
    }
    LIBC_NAMESPACE::File::unlock_list();
    ++reclamation_checks;
  }
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

TEST(LlvmLibcLinuxFileAllocationTest, OpenFailureClosesDescriptor) {
  for (unsigned failure = 1; failure <= 2; ++failure) {
    int expected_fd = next_descriptor();
    ASSERT_GE(expected_fd, 0);
    start_tracking(failure);
    auto result = LIBC_NAMESPACE::openfile("/dev/null", "r");
    tracking = false;
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ENOMEM);
    EXPECT_EQ(allocation_count, failure);
    EXPECT_EQ(live_allocations, 0U);
    EXPECT_EQ(next_descriptor(), expected_fd);
  }
}

TEST(LlvmLibcLinuxFileAllocationTest, FdopenFailurePreservesDescriptor) {
  for (unsigned failure = 1; failure <= 2; ++failure) {
    int fd = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(fd, 0);
    start_tracking(failure);
    auto result = LIBC_NAMESPACE::create_file_from_fd(fd, "r");
    tracking = false;
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ENOMEM);
    EXPECT_EQ(allocation_count, failure);
    EXPECT_EQ(live_allocations, 0U);
    EXPECT_GE(::fcntl(fd, F_GETFD), 0);
    EXPECT_EQ(::close(fd), 0);
  }
}

TEST(LlvmLibcLinuxFileAllocationTest, SeekFailureReleasesBufferAndObject) {
  int descriptors[2];
  ASSERT_EQ(::pipe(descriptors), 0);
  start_tracking(0);
  auto result = LIBC_NAMESPACE::create_file_from_fd(descriptors[1], "a");
  tracking = false;
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ESPIPE);
  EXPECT_EQ(allocation_count, 2U);
  EXPECT_EQ(live_allocations, 0U);
  EXPECT_GE(::fcntl(descriptors[1], F_GETFD), 0);
  EXPECT_EQ(::close(descriptors[0]), 0);
  EXPECT_EQ(::close(descriptors[1]), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, SuccessfulCloseReleasesResources) {
  int expected_fd = next_descriptor();
  ASSERT_GE(expected_fd, 0);
  start_tracking(0);
  auto result = LIBC_NAMESPACE::openfile("/dev/null", "r");
  tracking = false;
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(allocation_count, 2U);
  EXPECT_EQ(live_allocations, 2U);
  EXPECT_EQ(result.value()->close(), 0);
  EXPECT_EQ(live_allocations, 0U);
  EXPECT_EQ(next_descriptor(), expected_fd);
}

TEST(LlvmLibcLinuxFileAllocationTest, CloseUnpublishesBeforeReclamation) {
  auto result = LIBC_NAMESPACE::openfile("/dev/null", "w");
  ASSERT_TRUE(result.has_value());
  auto *file = result.value();
  ASSERT_EQ(file->write("buffered", 8).value, size_t(8));
  closing_file = file;
  published_at_reclamation = false;
  reclamation_checks = 0;
  int status = file->close();
  closing_file = nullptr;
  EXPECT_EQ(status, 0);
  EXPECT_EQ(reclamation_checks, 2U);
  EXPECT_FALSE(published_at_reclamation);
}

TEST(LlvmLibcLinuxFileAllocationTest, ClosePreservesNonOwnedObject) {
  int fd = ::open("/dev/null", O_WRONLY);
  ASSERT_GE(fd, 0);
  unsigned char buffer[32];
  LIBC_NAMESPACE::LinuxFile file(fd, buffer, sizeof(buffer), _IOFBF, false,
                                LIBC_NAMESPACE::File::mode_flags("w"), false);
  ASSERT_EQ(file.write("buffered", 8).value, size_t(8));
  EXPECT_EQ(file.close(), 0);
  EXPECT_EQ(file.get_fd(), -1);
  // Global flushing can still visit the static standard-stream object.
  EXPECT_EQ(file.flush(), 0);
}
