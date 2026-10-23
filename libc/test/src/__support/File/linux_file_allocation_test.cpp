//===-- Tests for LinuxFile allocation failure cleanup --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/File/linux/file.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/fclose.h"
#include "src/stdio/fmemopen.h"
#include "src/stdio/freopen.h"
#include "test/UnitTest/Test.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

namespace {
bool tracking;
unsigned allocation_count, fail_at, live_allocations;
void *allocations[4];
LIBC_NAMESPACE::File *closing_file;
bool published_at_reclamation;
unsigned reclamation_checks;
int opening_fd = -1;
int flags_at_allocation = -1;

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
  // The first allocation follows the open syscall, before File publication.
  if (opening_fd >= 0 && flags_at_allocation < 0)
    flags_at_allocation = ::fcntl(opening_fd, F_GETFD);
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
  const bool modes[] = {false, true};
  for (bool cloexec : modes) {
    for (unsigned failure = 1; failure <= 2; ++failure) {
      int expected_fd = next_descriptor();
      ASSERT_GE(expected_fd, 0);
      start_tracking(failure);
      auto result = LIBC_NAMESPACE::openfile("/dev/null", "r", cloexec);
      tracking = false;
      ASSERT_FALSE(result.has_value());
      EXPECT_EQ(result.error(), ENOMEM);
      EXPECT_EQ(allocation_count, failure);
      EXPECT_EQ(live_allocations, 0U);
      EXPECT_EQ(next_descriptor(), expected_fd);
    }
  }
}

TEST(LlvmLibcLinuxFileAllocationTest, CloseOnExecAtFirstAllocation) {
  const bool modes[] = {false, true};
  for (bool cloexec : modes) {
    opening_fd = next_descriptor();
    ASSERT_GE(opening_fd, 0);
    flags_at_allocation = -1;
    auto result = LIBC_NAMESPACE::openfile("/dev/null", "r", cloexec);
    opening_fd = -1;
    ASSERT_TRUE(result.has_value());
    ASSERT_GE(flags_at_allocation, 0);
    EXPECT_EQ(flags_at_allocation & FD_CLOEXEC, cloexec ? FD_CLOEXEC : 0);
    EXPECT_EQ(result.value()->close(), 0);
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

TEST(LlvmLibcLinuxFileAllocationTest, ModeCloseOnExecBeforePublication) {
  const char *modes[] = {"re", "rbe", "r+e", "we", "ae"};
  for (const char *mode : modes) {
    opening_fd = next_descriptor();
    ASSERT_GE(opening_fd, 0);
    flags_at_allocation = -1;
    auto result = LIBC_NAMESPACE::openfile("/dev/null", mode);
    opening_fd = -1;
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(flags_at_allocation & FD_CLOEXEC, FD_CLOEXEC);
    EXPECT_EQ(result.value()->close(), 0);
  }
}

TEST(LlvmLibcLinuxFileAllocationTest, ReopenPreservesCloseOnExec) {
  auto result = LIBC_NAMESPACE::openfile("/dev/null", "r");
  ASSERT_TRUE(result.has_value());
  auto *file = result.value();
  int fd = LIBC_NAMESPACE::get_fileno(file);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(::fcntl(fd, F_GETFD) & FD_CLOEXEC, 0);
  ASSERT_EQ(LIBC_NAMESPACE::reopenfile(file, "/dev/null", "re"), 0);
  EXPECT_EQ(LIBC_NAMESPACE::get_fileno(file), fd);
  EXPECT_EQ(::fcntl(fd, F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
  ASSERT_EQ(LIBC_NAMESPACE::reopenfile(file, "/dev/null", "r"), 0);
  EXPECT_EQ(::fcntl(fd, F_GETFD) & FD_CLOEXEC, 0);
  ASSERT_EQ(LIBC_NAMESPACE::reopenfile(file, nullptr, "re"), 0);
  EXPECT_EQ(::fcntl(fd, F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
  ASSERT_EQ(LIBC_NAMESPACE::reopenfile(file, nullptr, "r"), 0);
  EXPECT_EQ(::fcntl(fd, F_GETFD) & FD_CLOEXEC, 0);
  ASSERT_EQ(LIBC_NAMESPACE::reopenfile(file, nullptr, "rbe"), 0);
  EXPECT_EQ(::fcntl(fd, F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
  EXPECT_EQ(file->close(), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, ExclusiveCreatePreservesExistingFile) {
  char path[] = "testdata/exclusive-create-XXXXXX";
  int fd = ::mkstemp(path);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(::write(fd, "keep", 4), ssize_t(4));
  ASSERT_EQ(::close(fd), 0);

  const char *modes[] = {"wx", "w+x", "wbx", "w+bx", "wb+x", "wxe"};
  for (const char *mode : modes) {
    auto existing = LIBC_NAMESPACE::openfile(path, mode);
    ASSERT_FALSE(existing.has_value());
    EXPECT_EQ(existing.error(), EEXIST);
    fd = ::open(path, O_RDONLY);
    ASSERT_GE(fd, 0);
    char contents[5] = {};
    ASSERT_EQ(::read(fd, contents, sizeof(contents)), ssize_t(4));
    EXPECT_STREQ(contents, "keep");
    ASSERT_EQ(::close(fd), 0);

    ASSERT_EQ(::unlink(path), 0);
    auto created = LIBC_NAMESPACE::openfile(path, mode);
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created.value()->write("keep", 4).value, size_t(4));
    EXPECT_EQ(created.value()->close(), 0);
  }
  EXPECT_EQ(::unlink(path), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, ExclusiveReopen) {
  char path[] = "testdata/exclusive-reopen-XXXXXX";
  int fd = ::mkstemp(path);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(::write(fd, "keep", 4), ssize_t(4));
  ASSERT_EQ(::close(fd), 0);

  const char *modes[] = {"wx", "w+x", "wb+x", "wxe"};
  for (const char *mode : modes) {
    // Both pathname and NULL-path reopening must fail without truncation.
    const char *paths[] = {path, nullptr};
    for (const char *name : paths) {
      auto stream = LIBC_NAMESPACE::openfile(path, "r+");
      ASSERT_TRUE(stream.has_value());
      fd = LIBC_NAMESPACE::get_fileno(stream.value());
      EXPECT_EQ(LIBC_NAMESPACE::reopenfile(stream.value(), name, mode), EEXIST);
      EXPECT_EQ(LIBC_NAMESPACE::get_fileno(stream.value()), -1);
      EXPECT_EQ(::fcntl(fd, F_GETFD), -1);
      // Dispose of the internal object whose descriptor has already closed.
      EXPECT_EQ(stream.value()->close(), EBADF);
      fd = ::open(path, O_RDONLY);
      ASSERT_GE(fd, 0);
      char contents[5] = {};
      ASSERT_EQ(::read(fd, contents, sizeof(contents)), ssize_t(4));
      EXPECT_STREQ(contents, "keep");
      ASSERT_EQ(::close(fd), 0);
    }
    auto stream = LIBC_NAMESPACE::openfile("/dev/null", "r");
    ASSERT_TRUE(stream.has_value());
    ASSERT_EQ(::unlink(path), 0);
    ASSERT_EQ(LIBC_NAMESPACE::reopenfile(stream.value(), path, mode), 0);
    EXPECT_EQ(stream.value()->write("keep", 4).value, size_t(4));
    EXPECT_EQ(stream.value()->close(), 0);
  }
  EXPECT_EQ(::unlink(path), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, FdopenPreservesExistingCloseOnExec) {
  int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  ASSERT_GE(fd, 0);
  auto result = LIBC_NAMESPACE::create_file_from_fd(fd, "r");
  ASSERT_TRUE(result.has_value());
  EXPECT_NE(::fcntl(fd, F_GETFD) & FD_CLOEXEC, 0);
  EXPECT_EQ(result.value()->close(), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, AppendPipeCloseReleasesBufferAndObject) {
  int descriptors[2];
  ASSERT_EQ(::pipe(descriptors), 0);
  start_tracking(0);
  auto result = LIBC_NAMESPACE::create_file_from_fd(descriptors[1], "a");
  tracking = false;
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(allocation_count, 2U);
  EXPECT_EQ(live_allocations, 2U);
  EXPECT_GE(::fcntl(descriptors[1], F_GETFD), 0);
  EXPECT_EQ(result.value()->close(), 0);
  EXPECT_EQ(live_allocations, 0U);
  EXPECT_EQ(::fcntl(descriptors[1], F_GETFD), -1);
  EXPECT_EQ(::close(descriptors[0]), 0);
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

TEST(LlvmLibcLinuxFileAllocationTest, FailedReopenConsumesOwnedResources) {
  struct Case {
    const char *path;
    const char *mode;
    int error;
  };
  const Case cases[] = {{"/dev/null", "invalid", EINVAL},
                        {"/dev/null/missing", "r", ENOTDIR},
                        {nullptr, "w", EBADF},
                        {nullptr, "wx", EEXIST}};
  for (const auto &entry : cases) {
    int expected_fd = next_descriptor();
    ASSERT_GE(expected_fd, 0);
    start_tracking(0);
    auto result = LIBC_NAMESPACE::openfile("/dev/null", "r");
    tracking = false;
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(live_allocations, 2U);
    closing_file = result.value();
    published_at_reclamation = false;
    reclamation_checks = 0;
    auto *reopened = LIBC_NAMESPACE::freopen(
        entry.path, entry.mode, reinterpret_cast<FILE *>(result.value()));
    int error = LIBC_NAMESPACE::libc_errno;
    closing_file = nullptr;
    EXPECT_EQ(reopened, static_cast<FILE *>(nullptr));
    EXPECT_EQ(error, entry.error);
    EXPECT_EQ(live_allocations, 0U);
    EXPECT_EQ(reclamation_checks, 2U);
    EXPECT_FALSE(published_at_reclamation);
    EXPECT_EQ(next_descriptor(), expected_fd);
  }
}

TEST(LlvmLibcLinuxFileAllocationTest, FailedReopenPreservesNonOwnedObject) {
  int fd = ::open("/dev/null", O_RDONLY);
  ASSERT_GE(fd, 0);
  unsigned char buffer[32];
  LIBC_NAMESPACE::LinuxFile file(fd, buffer, sizeof(buffer), _IOFBF, false,
                                LIBC_NAMESPACE::File::mode_flags("r"), false);
  EXPECT_EQ(LIBC_NAMESPACE::freopen(nullptr, "w",
                                   reinterpret_cast<FILE *>(&file)),
            static_cast<FILE *>(nullptr));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  EXPECT_EQ(::fcntl(fd, F_GETFD), -1);
  // Inspect the internal static object, not the invalid public FILE handle.
  EXPECT_EQ(file.get_fd(), -1);
  EXPECT_EQ(file.flush(), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, ReopenNeedsNoSpareDescriptor) {
  auto result = LIBC_NAMESPACE::openfile("/dev/null", "r");
  ASSERT_TRUE(result.has_value());
  auto *file = result.value();
  int fd = LIBC_NAMESPACE::get_fileno(file);
  struct rlimit original;
  ASSERT_EQ(::getrlimit(RLIMIT_NOFILE, &original), 0);
  struct rlimit limited = original;
  limited.rlim_cur = static_cast<rlim_t>(fd) + 1;
  ASSERT_EQ(::setrlimit(RLIMIT_NOFILE, &limited), 0);
  auto *reopened = LIBC_NAMESPACE::freopen(
      "/dev/null", "re", reinterpret_cast<FILE *>(file));
  // Restore the process limit before assertions or other test infrastructure.
  int restored = ::setrlimit(RLIMIT_NOFILE, &original);
  ASSERT_EQ(restored, 0);
  ASSERT_EQ(reopened, reinterpret_cast<FILE *>(file));
  EXPECT_EQ(LIBC_NAMESPACE::get_fileno(file), fd);
  EXPECT_EQ(::fcntl(fd, F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
  EXPECT_EQ(file->close(), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, ReplacementAllocationFailureConsumesBoth) {
  int expected_fd = next_descriptor();
  ASSERT_GE(expected_fd, 0);
  start_tracking(3);
  // The memory buffer and File succeed; allocation of the replacement backend
  // fails after its descriptor has opened and the old memory has been freed.
  FILE *stream = LIBC_NAMESPACE::fmemopen(nullptr, 32, "w+");
  if (!stream) {
    tracking = false;
    ASSERT_NE(stream, nullptr);
  }
  auto *replacement = LIBC_NAMESPACE::freopen("/dev/null", "w", stream);
  int error = LIBC_NAMESPACE::libc_errno;
  tracking = false;
  EXPECT_EQ(replacement, static_cast<FILE *>(nullptr));
  EXPECT_EQ(error, ENOMEM);
  EXPECT_EQ(allocation_count, 3U);
  EXPECT_EQ(live_allocations, 0U);
  EXPECT_EQ(next_descriptor(), expected_fd);
}

TEST(LlvmLibcLinuxFileAllocationTest, ReplacementCloseReleasesBothObjects) {
  int expected_fd = next_descriptor();
  ASSERT_GE(expected_fd, 0);
  start_tracking(0);
  FILE *stream = LIBC_NAMESPACE::fmemopen(nullptr, 32, "w+");
  tracking = false;
  ASSERT_NE(stream, nullptr);
  ASSERT_EQ(live_allocations, 2U);
  tracking = true;
  auto *replacement = LIBC_NAMESPACE::freopen("/dev/null", "w", stream);
  tracking = false;
  ASSERT_EQ(replacement, stream);
  EXPECT_EQ(allocation_count, 3U);
  // The old backing memory is gone; the public object and new backend remain.
  EXPECT_EQ(live_allocations, 2U);
  EXPECT_EQ(reinterpret_cast<LIBC_NAMESPACE::File *>(replacement)->close(), 0);
  EXPECT_EQ(live_allocations, 0U);
  EXPECT_EQ(next_descriptor(), expected_fd);
}

TEST(LlvmLibcLinuxFileAllocationTest, SetBufferFailurePreservesCallerStorage) {
  int fd = ::open("/dev/null", O_WRONLY);
  ASSERT_GE(fd, 0);
  unsigned char storage[16] = {};
  LIBC_NAMESPACE::LinuxFile file(fd, storage + 1, sizeof(storage) - 2, _IOLBF,
                                 false, LIBC_NAMESPACE::File::mode_flags("w"),
                                 false);
  storage[0] = storage[15] = 0x5a;
  start_tracking(1);
  int result = file.set_buffer(nullptr, 64, _IOFBF);
  tracking = false;
  EXPECT_EQ(result, ENOMEM);
  EXPECT_EQ(allocation_count, 1U);
  EXPECT_EQ(live_allocations, 0U);
  EXPECT_EQ(file.buffer_capacity(), size_t(14));
  EXPECT_TRUE(file.line_buffered());
  ASSERT_EQ(file.write("old buffer", 10).value, size_t(10));
  EXPECT_EQ(storage[1], static_cast<unsigned char>('o'));
  EXPECT_EQ(storage[10], static_cast<unsigned char>('r'));
  EXPECT_EQ(storage[0], static_cast<unsigned char>(0x5a));
  EXPECT_EQ(storage[15], static_cast<unsigned char>(0x5a));
  EXPECT_EQ(file.close(), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, FcloseFlushFailureConsumesResources) {
  int expected_fd = next_descriptor();
  ASSERT_GE(expected_fd, 0);
  start_tracking(0);
  auto result = LIBC_NAMESPACE::openfile("/dev/full", "w");
  tracking = false;
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(live_allocations, 2U);
  auto *file = result.value();
  ASSERT_EQ(file->write("buffered", 8).value, size_t(8));
  closing_file = file;
  published_at_reclamation = false;
  reclamation_checks = 0;
  int status = LIBC_NAMESPACE::fclose(reinterpret_cast<FILE *>(file));
  int error = LIBC_NAMESPACE::libc_errno;
  closing_file = nullptr;
  EXPECT_EQ(status, EOF);
  EXPECT_EQ(error, ENOSPC);
  EXPECT_EQ(live_allocations, 0U);
  EXPECT_EQ(reclamation_checks, 2U);
  EXPECT_FALSE(published_at_reclamation);
  EXPECT_EQ(::fcntl(expected_fd, F_GETFD), -1);
  int reused = ::open("/dev/null", O_RDONLY);
  EXPECT_EQ(reused, expected_fd);
  EXPECT_GE(::fcntl(reused, F_GETFD), 0);
  EXPECT_EQ(::close(reused), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, AutomaticBufferModeUsesRealDevice) {
  int master = ::posix_openpt(O_RDWR | O_NOCTTY);
  ASSERT_GE(master, 0);
  ASSERT_EQ(::grantpt(master), 0);
  ASSERT_EQ(::unlockpt(master), 0);
  char *name = ::ptsname(master);
  ASSERT_NE(name, nullptr);
  const char *paths[] = {"/dev/null", name};
  for (unsigned i = 0; i < 2; ++i) {
    int fd = ::open(paths[i], O_RDWR | O_NOCTTY);
    ASSERT_GE(fd, 0);
    unsigned char buffer[32];
    LIBC_NAMESPACE::LinuxFile file(
        fd, buffer, sizeof(buffer), LIBC_NAMESPACE::File::AUTO_BUFFERING, false,
        LIBC_NAMESPACE::File::mode_flags("r+"), false);
    LIBC_NAMESPACE::libc_errno = EDOM;
    EXPECT_EQ(file.line_buffered(), i == 1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    EXPECT_EQ(file.close(), 0);
  }
  EXPECT_EQ(::close(master), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, ExplicitBufferModeOverridesDetection) {
  int fd = ::open("/dev/null", O_WRONLY);
  ASSERT_GE(fd, 0);
  unsigned char buffer[32];
  LIBC_NAMESPACE::LinuxFile file(fd, buffer, sizeof(buffer),
                                 LIBC_NAMESPACE::File::AUTO_BUFFERING, false,
                                 LIBC_NAMESPACE::File::mode_flags("w"), false);
  EXPECT_EQ(file.set_buffer(buffer, sizeof(buffer), _IOLBF), 0);
  ASSERT_EQ(file.write("x", 1).value, size_t(1));
  EXPECT_TRUE(file.line_buffered());
  EXPECT_EQ(file.close(), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, OpenAndFdopenSelectDeviceBuffering) {
  int master = ::posix_openpt(O_RDWR | O_NOCTTY);
  ASSERT_GE(master, 0);
  ASSERT_EQ(::grantpt(master), 0);
  ASSERT_EQ(::unlockpt(master), 0);
  char *name = ::ptsname(master);
  ASSERT_NE(name, nullptr);
  const bool modes[] = {false, true};
  for (bool explicit_mode : modes) {
    auto opened = LIBC_NAMESPACE::openfile(name, "r+");
    ASSERT_TRUE(opened.has_value());
    auto *file = opened.value();
    if (explicit_mode)
      ASSERT_EQ(file->set_buffer(nullptr, 64, _IOFBF), 0);
    EXPECT_EQ(file->line_buffered(), !explicit_mode);
    EXPECT_EQ(file->close(), 0);

    int fd = ::open(name, O_RDWR | O_NOCTTY);
    ASSERT_GE(fd, 0);
    auto associated = LIBC_NAMESPACE::create_file_from_fd(fd, "r+");
    ASSERT_TRUE(associated.has_value());
    if (explicit_mode)
      ASSERT_EQ(associated.value()->set_buffer(nullptr, 64, _IOFBF), 0);
    EXPECT_EQ(associated.value()->line_buffered(), !explicit_mode);
    EXPECT_EQ(associated.value()->close(), 0);
  }
  int pipefd[2];
  ASSERT_EQ(::pipe(pipefd), 0);
  auto reader = LIBC_NAMESPACE::create_file_from_fd(pipefd[0], "r");
  ASSERT_TRUE(reader.has_value());
  EXPECT_FALSE(reader.value()->line_buffered());
  EXPECT_EQ(reader.value()->close(), 0);
  EXPECT_EQ(::close(pipefd[1]), 0);
  EXPECT_EQ(::close(master), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, FdopenAppendPreservesPosition) {
  char path[] = "fdopen-XXXXXX";
  int fd = ::mkstemp(path);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(::unlink(path), 0);
  ASSERT_EQ(::write(fd, "abc", 3), ssize_t(3));
  ASSERT_EQ(::lseek(fd, 1, SEEK_SET), off_t(1));
  auto result = LIBC_NAMESPACE::create_file_from_fd(fd, "a+");
  ASSERT_TRUE(result.has_value());
  auto *file = result.value();
  EXPECT_EQ(file->tell().value(), off_t(1));
  char c = 0;
  EXPECT_EQ(file->read(&c, 1).value, size_t(1));
  EXPECT_EQ(c, 'b');
  ASSERT_TRUE(file->seek(0, SEEK_SET).has_value());
  ASSERT_EQ(file->write("d", 1).value, size_t(1));
  EXPECT_EQ(file->close(), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, FdopenAppendAcceptsPipe) {
  int pipefd[2];
  ASSERT_EQ(::pipe(pipefd), 0);
  auto result = LIBC_NAMESPACE::create_file_from_fd(pipefd[1], "a");
  EXPECT_TRUE(result.has_value());
  if (result.has_value()) {
    EXPECT_EQ(result.value()->write("x", 1).value, size_t(1));
    EXPECT_EQ(result.value()->close(), 0);
    char c;
    EXPECT_EQ(::read(pipefd[0], &c, 1), ssize_t(1));
    EXPECT_EQ(c, 'x');
  } else {
    EXPECT_EQ(::close(pipefd[1]), 0);
  }
  EXPECT_EQ(::close(pipefd[0]), 0);
}

TEST(LlvmLibcLinuxFileAllocationTest, FdopenSetsCloseOnExec) {
  int fd = ::open("/dev/null", O_RDWR);
  ASSERT_GE(fd, 0);
  auto result = LIBC_NAMESPACE::create_file_from_fd(fd, "re");
  ASSERT_TRUE(result.has_value());
  EXPECT_NE(::fcntl(fd, F_GETFD) & FD_CLOEXEC, 0);
  EXPECT_EQ(result.value()->close(), 0);
}
