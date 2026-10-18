//===-- Integration tests for posix_spawnp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/CPP/stringstream.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/open.h"
#include "src/spawn/posix_spawn_file_actions_addopen.h"
#include "src/spawn/posix_spawn_file_actions_destroy.h"
#include "src/spawn/posix_spawn_file_actions_init.h"
#include "src/spawn/posix_spawnp.h"
#include "src/stdlib/getenv.h"
#include "src/stdlib/mkdtemp.h"
#include "src/stdlib/setenv.h"
#include "src/stdlib/unsetenv.h"
#include "src/sys/stat/chmod.h"
#include "src/sys/stat/mkdir.h"
#include "src/sys/wait/waitpid.h"
#include "src/unistd/close.h"
#include "src/unistd/read.h"
#include "src/unistd/rmdir.h"
#include "src/unistd/symlink.h"
#include "src/unistd/unlink.h"
#include "src/unistd/write.h"
#include "test/IntegrationTest/test.h"
#include <fcntl.h>
#include <sys/wait.h>

namespace {
void join(char (&buffer)[512], const char *dir, const char *name) {
  LIBC_NAMESPACE::cpp::StringStream stream(buffer);
  stream << dir << '/' << name << '\0';
}

void make_file(const char *path, mode_t mode) {
  int fd = LIBC_NAMESPACE::open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  ASSERT_TRUE(fd >= 0);
  ASSERT_EQ(LIBC_NAMESPACE::write(fd, "exit 0\n", 7), 7);
  ASSERT_EQ(LIBC_NAMESPACE::close(fd), 0);
  ASSERT_EQ(LIBC_NAMESPACE::chmod(path, mode), 0);
}

void run(const char *file, posix_spawn_file_actions_t *actions, int expected) {
  char zero[] = "spawn-custom-zero", child[] = "child";
  char *argv[] = {zero, child, nullptr};
  char marker[] = "SPAWNP=explicit", path[] = "PATH=/no-child-path";
  char *env[] = {marker, path, nullptr};
  pid_t pid = -1;
  libc_errno = EDOM;
  ASSERT_EQ(
      LIBC_NAMESPACE::posix_spawnp(&pid, file, actions, nullptr, argv, env), 0);
  ASSERT_EQ(libc_errno, EDOM);
  ASSERT_TRUE(pid > 0);
  int status;
  ASSERT_EQ(LIBC_NAMESPACE::waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), expected);
}
} // namespace

TEST_MAIN(int argc, char **argv, char **) {
  if (argc > 1) {
    ASSERT_EQ(argc, 2);
    ASSERT_STREQ(argv[0], "spawn-custom-zero");
    ASSERT_STREQ(argv[1], "child");
    ASSERT_STREQ(LIBC_NAMESPACE::getenv("SPAWNP"), "explicit");
    ASSERT_STREQ(LIBC_NAMESPACE::getenv("PATH"), "/no-child-path");
    char c;
    ASSERT_EQ(LIBC_NAMESPACE::read(9, &c, 1), 1);
    ASSERT_EQ(c, 'e');
    return 23;
  }

  char dir[] = "spawnp.XXXXXX";
  ASSERT_NE(LIBC_NAMESPACE::mkdtemp(dir), nullptr);
  char program[512], denied_dir[512], denied[512], not_dir[512], data[512];
  join(program, dir, "program");
  join(denied_dir, dir, "denied");
  join(denied, denied_dir, "program");
  join(not_dir, dir, "not-directory");
  join(data, dir, "data");
  ASSERT_EQ(LIBC_NAMESPACE::mkdir(denied_dir, 0700), 0);
  ASSERT_EQ(LIBC_NAMESPACE::symlink("/proc/self/exe", program), 0);
  make_file(denied, 0600);
  make_file(not_dir, 0600);
  make_file(data, 0600);

  posix_spawn_file_actions_t actions;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions), 0);
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_addopen(&actions, 9, data,
                                                             O_RDONLY, 0),
            0);

  char path[2048];
  LIBC_NAMESPACE::cpp::StringStream stream(path);
  stream << denied_dir << ':' << not_dir << ':' << dir << "/missing:" << dir
         << '\0';
  ASSERT_EQ(LIBC_NAMESPACE::setenv("PATH", path, 1), 0);
  run("program", &actions, 23); // EACCES, ENOTDIR, ENOENT, then success.
  run("absent", &actions, 127);
  ASSERT_EQ(LIBC_NAMESPACE::chmod(denied, 0700), 0);
  run("program", &actions, 127); // ENOEXEC stops search; no shell fallback.
  run(program, &actions, 23);    // A slash bypasses PATH.

  // Empty leading, middle, trailing and sole components mean cwd. Include a
  // failing relative entry to exercise advancing to a final empty component.
  char current[512];
  LIBC_NAMESPACE::cpp::StringStream current_stream(current);
  current_stream << dir << "-current" << '\0';
  ASSERT_EQ(LIBC_NAMESPACE::symlink("/proc/self/exe", current), 0);
  char leading[] = ":/missing", middle[] = "/missing::/missing";
  char trailing[] = "/missing:", empty[] = "";
  char *settings[] = {leading, middle, trailing, empty};
  for (char *setting : settings) {
    ASSERT_EQ(LIBC_NAMESPACE::setenv("PATH", setting, 1), 0);
    run(current, &actions, 23);
  }
  ASSERT_EQ(LIBC_NAMESPACE::unlink(current), 0);
  // Unset PATH uses the implementation default.
  ASSERT_EQ(LIBC_NAMESPACE::unsetenv("PATH"), 0);
  char zero[] = "true";
  char *args[] = {zero, nullptr};
  char *empty_env[] = {nullptr};
  pid_t pid = -1;
  ASSERT_EQ(LIBC_NAMESPACE::posix_spawnp(&pid, "true", nullptr, nullptr, args,
                                         empty_env),
            0);
  int status;
  ASSERT_EQ(LIBC_NAMESPACE::waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
  pid = -1;
  ASSERT_EQ(
      LIBC_NAMESPACE::posix_spawnp(&pid, "", nullptr, nullptr, args, empty_env),
      ENOENT);
  ASSERT_EQ(pid, -1);

  ASSERT_EQ(LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&actions), 0);
  ASSERT_EQ(LIBC_NAMESPACE::unlink(program), 0);
  ASSERT_EQ(LIBC_NAMESPACE::unlink(denied), 0);
  ASSERT_EQ(LIBC_NAMESPACE::unlink(not_dir), 0);
  ASSERT_EQ(LIBC_NAMESPACE::unlink(data), 0);
  ASSERT_EQ(LIBC_NAMESPACE::rmdir(denied_dir), 0);
  ASSERT_EQ(LIBC_NAMESPACE::rmdir(dir), 0);
  return 0;
}
