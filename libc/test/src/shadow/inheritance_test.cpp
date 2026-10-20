//===-- Account descriptor inheritance tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pwd/flat_file_db.h"
#include "src/shadow/lookup.h"
#include "test/UnitTest/Test.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
struct Probe {
  int fd;
  bool inherited;
  int status = -1;
};

void *exec_probe(void *arg) {
  auto &probe = *static_cast<Probe *>(arg);
  char descriptor[32];
  ::snprintf(descriptor, sizeof(descriptor), "%d", probe.fd);
  const char *script = probe.inherited ? "test -e /proc/self/fd/$1"
                                       : "test ! -e /proc/self/fd/$1";
  pid_t child = ::fork();
  if (child == 0) {
    ::execl("/bin/sh", "sh", "-c", script, "descriptor-probe", descriptor,
            static_cast<char *>(nullptr));
    ::_exit(127);
  }
  if (child > 0) {
    pid_t waited;
    do {
      waited = ::waitpid(child, &probe.status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child)
      probe.status = -1;
  }
  return nullptr;
}

bool check_exec(int fd, bool inherited) {
  Probe probe{fd, inherited};
  pthread_t thread;
  if (::pthread_create(&thread, nullptr, exec_probe, &probe))
    return false;
  if (::pthread_join(thread, nullptr))
    return false;
  return probe.status == 0;
}
} // namespace

TEST(LlvmLibcShadowInheritanceTest,
     QueryDescriptorDoesNotSurviveConcurrentExec) {
  auto path = libc_make_test_file_path("shadow-inheritance.test");
  int fd = ::open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
  ASSERT_GE(fd, 0);
  const char record[] = "alice:!:123:0:90:7:30:20000:\n";
  ASSERT_EQ(::write(fd, record, sizeof(record) - 1),
            ssize_t(sizeof(record) - 1));
  struct stat expected;
  ASSERT_EQ(::fstat(fd, &expected), 0);

  // Positive control: exec really inherits ordinary, non-CLOEXEC descriptors.
  ASSERT_TRUE(check_exec(fd, true));
  ASSERT_EQ(::close(fd), 0);

  spwd entry{}, *result = nullptr;
  char buffer[128];
  bool observed = false;
  int status = LIBC_NAMESPACE::shadow_db::lookup_shadow(
      [&](const spwd &) {
        // This callback runs while the production query still owns its File.
        struct stat actual{};
        EXPECT_EQ(::fstat(fd, &actual), 0);
        EXPECT_EQ(actual.st_dev, expected.st_dev);
        EXPECT_EQ(actual.st_ino, expected.st_ino);
        int flags = ::fcntl(fd, F_GETFD);
        EXPECT_GE(flags, 0);
        EXPECT_NE(flags & FD_CLOEXEC, 0);
        observed = check_exec(fd, false);
        return true;
      },
      &entry, buffer, sizeof(buffer), &result, path);
  EXPECT_EQ(status, 0);
  EXPECT_EQ(result, &entry);
  EXPECT_TRUE(observed);
  EXPECT_EQ(::fcntl(fd, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);

  LIBC_NAMESPACE::pwd::FlatFileDatabase<spwd> database(path);
  ASSERT_TRUE(database.setdb().has_value());
  EXPECT_TRUE(check_exec(fd, false));
  EXPECT_NE(::fcntl(fd, F_GETFD) & FD_CLOEXEC, 0);
  ASSERT_TRUE(database.enddb().has_value());
  EXPECT_EQ(::fcntl(fd, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
  EXPECT_EQ(::unlink(path), 0);
}
