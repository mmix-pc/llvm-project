//===-- Integration tests for pipe streams --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/errno_macros.h"
#include "src/__support/CPP/stringstream.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/signal/sigaction.h"
#include "src/spawn/posix_spawn.h"
#include "src/stdio/fgetc.h"
#include "src/stdio/fileno.h"
#include "src/stdio/fputs.h"
#include "src/stdio/linux/pipe_fork.h"
#include "src/stdio/pclose.h"
#include "src/stdio/popen.h"
#include "src/sys/resource/getrlimit.h"
#include "src/sys/resource/setrlimit.h"
#include "src/sys/wait/waitpid.h"
#include "src/unistd/alarm.h"
#include "src/unistd/close.h"
#include "src/unistd/environ.h"
#include "src/unistd/execve.h"
#include "src/unistd/fork.h"
#include "src/wchar/fwide.h"
#include "test/IntegrationTest/test.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>

namespace {
int prepare_calls = 0;
int parent_calls = 0;
void on_prepare() { ++prepare_calls; }
void on_parent() { ++parent_calls; }

void exited(int status, int code) {
  ASSERT_TRUE(status >= 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), code);
}

void reading(const char *mode) {
  ::FILE *f = LIBC_NAMESPACE::popen("printf abc; exit 7", mode);
  ASSERT_NE(f, nullptr);
  ASSERT_TRUE(LIBC_NAMESPACE::fwide(f, 0) < 0);
  int fd = LIBC_NAMESPACE::fileno(f);
  ASSERT_TRUE(fd >= 0);
  int flags = LIBC_NAMESPACE::fcntl(fd, F_GETFD);
  ASSERT_TRUE(flags >= 0);
  ASSERT_EQ(bool(flags & FD_CLOEXEC), mode[1] == 'e');
  ASSERT_EQ(LIBC_NAMESPACE::fgetc(f), 'a');
  // Unread buffered input must not cause an ESPIPE failure during close.
  exited(LIBC_NAMESPACE::pclose(f), 7);
  ASSERT_EQ(LIBC_NAMESPACE::fcntl(fd, F_GETFD), -1);
  ASSERT_EQ(libc_errno, EBADF);
}

void writing(const char *mode) {
  ::FILE *f = LIBC_NAMESPACE::popen(
      "IFS= read -r value; test \"$value\" = payload", mode);
  ASSERT_NE(f, nullptr);
  ASSERT_TRUE(LIBC_NAMESPACE::fwide(f, 0) < 0);
  int flags = LIBC_NAMESPACE::fcntl(LIBC_NAMESPACE::fileno(f), F_GETFD);
  ASSERT_TRUE(flags >= 0);
  ASSERT_EQ(bool(flags & FD_CLOEXEC), mode[1] == 'e');
  ASSERT_TRUE(LIBC_NAMESPACE::fputs("payload\n", f) >= 0);
  exited(LIBC_NAMESPACE::pclose(f), 0); // Flush and EOF precede wait.
}

void prior_descriptors() {
  ::FILE *first = LIBC_NAMESPACE::popen("read value", "w");
  ASSERT_NE(first, nullptr);
  int fd = LIBC_NAMESPACE::fileno(first);
  char command[128];
  LIBC_NAMESPACE::cpp::StringStream stream(command);
  stream << "test ! -e /proc/self/fd/" << fd << '\0';
  ::FILE *second = LIBC_NAMESPACE::popen(command, "r");
  ASSERT_NE(second, nullptr);
  exited(LIBC_NAMESPACE::pclose(second), 0);
  ASSERT_TRUE(LIBC_NAMESPACE::fputs("done\n", first) >= 0);
  exited(LIBC_NAMESPACE::pclose(first), 0);
}

void low_descriptors() {
  char name[] = "popen-test", low[] = "low", collision[] = "collision";
  char *modes[] = {low, collision};
  for (char *mode : modes) {
    char *args[] = {name, mode, nullptr};
    pid_t pid = -1;
    ASSERT_EQ(LIBC_NAMESPACE::posix_spawn(&pid, "/proc/self/exe", nullptr,
                                          nullptr, args,
                                          LIBC_NAMESPACE::environ),
              0);
    int status;
    ASSERT_EQ(LIBC_NAMESPACE::waitpid(pid, &status, 0), pid);
    exited(status, 0);
  }
}

void inherited_registry() {
  ::FILE *f = LIBC_NAMESPACE::popen("printf z", "r");
  ASSERT_NE(f, nullptr);
  char name[] = "popen-test", mode[] = "exec-child";
  char *args[] = {name, mode, nullptr};
  pid_t pid = LIBC_NAMESPACE::fork();
  ASSERT_TRUE(pid >= 0);
  if (!pid) {
#ifdef LIBC_TEST_EXEC_ONLY_FORK_CHILD
    // The coordinated MMIX fork child supports exec/_exit only. Exercise the
    // callback completion boundary without using stdio or creating more
    // children.
    LIBC_NAMESPACE::execve("/proc/self/exe", args, LIBC_NAMESPACE::environ);
    LIBC_NAMESPACE::internal::exit(99);
#else
    (void)args;
    ASSERT_EQ(LIBC_NAMESPACE::pclose(f), -1);
    ASSERT_EQ(libc_errno, ECHILD); // This PID belongs to the parent.
    reading("r"); // The inherited registry lock must be usable.
    LIBC_NAMESPACE::internal::exit(0);
#endif
  }
  int status;
  ASSERT_EQ(LIBC_NAMESPACE::waitpid(pid, &status, 0), pid);
  exited(status, 0);
  ASSERT_EQ(LIBC_NAMESPACE::fgetc(f), 'z');
  exited(LIBC_NAMESPACE::pclose(f), 0);
}

volatile sig_atomic_t interrupted = 0;
void on_alarm(int) { interrupted = 1; }
void interrupted_wait() {
  struct sigaction action{}, old{};
  action.sa_handler = on_alarm;
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGALRM, &action, &old), 0);
  ::FILE *f = LIBC_NAMESPACE::popen("sleep 2; exit 6", "r");
  ASSERT_NE(f, nullptr);
  LIBC_NAMESPACE::alarm(1);
  exited(LIBC_NAMESPACE::pclose(f), 6);
  LIBC_NAMESPACE::alarm(0);
  ASSERT_EQ(interrupted, 1);
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGALRM, &old, nullptr), 0);
}

void failed_pipe() {
  struct rlimit saved;
  ASSERT_EQ(LIBC_NAMESPACE::getrlimit(RLIMIT_NOFILE, &saved), 0);
  struct rlimit limit = saved;
  limit.rlim_cur = 0;
  ASSERT_EQ(LIBC_NAMESPACE::setrlimit(RLIMIT_NOFILE, &limit), 0);
  for (int i = 0; i < 8; ++i) {
    ASSERT_EQ(LIBC_NAMESPACE::popen("exit 99", "r"), nullptr);
    ASSERT_EQ(libc_errno, EMFILE);
  }
  ASSERT_EQ(LIBC_NAMESPACE::setrlimit(RLIMIT_NOFILE, &saved), 0);
  reading("r"); // Failure did not leave the registry locked or corrupt.
}

void failed_flush() {
  struct sigaction ignore{}, old{};
  ignore.sa_handler = SIG_IGN;
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGPIPE, &ignore, &old), 0);
  ::FILE *f = LIBC_NAMESPACE::popen("exit 0", "w");
  ASSERT_NE(f, nullptr);
  int fd = LIBC_NAMESPACE::fileno(f);
  // Observe exit without stealing pclose's wait status. There are no other
  // children in this test at this point.
  siginfo_t info{};
  long result;
  do {
    result = LIBC_NAMESPACE::syscall_impl<long>(SYS_waitid, P_ALL, 0, &info,
                                                WEXITED | WNOWAIT, nullptr);
  } while (result == -EINTR);
  ASSERT_EQ(result, 0);
  ASSERT_TRUE(LIBC_NAMESPACE::fputs("pending output", f) >= 0);
  ASSERT_EQ(LIBC_NAMESPACE::pclose(f), -1);
  ASSERT_EQ(libc_errno, EPIPE);
  ASSERT_EQ(LIBC_NAMESPACE::fcntl(fd, F_GETFD), -1);
  ASSERT_EQ(libc_errno, EBADF);
  int status;
  ASSERT_EQ(LIBC_NAMESPACE::waitpid(-1, &status, WNOHANG), -1);
  ASSERT_EQ(libc_errno, ECHILD);
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGPIPE, &old, nullptr), 0);
}

void autoreaped_child() {
  struct sigaction ignore{}, old{};
  ignore.sa_handler = SIG_IGN;
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGCHLD, &ignore, &old), 0);
  ::FILE *f = LIBC_NAMESPACE::popen("exit 0", "r");
  ASSERT_NE(f, nullptr);
  int fd = LIBC_NAMESPACE::fileno(f);
  ASSERT_EQ(LIBC_NAMESPACE::pclose(f), -1);
  ASSERT_EQ(libc_errno, ECHILD);
  ASSERT_EQ(LIBC_NAMESPACE::fcntl(fd, F_GETFD), -1);
  ASSERT_EQ(libc_errno, EBADF);
  ASSERT_EQ(LIBC_NAMESPACE::sigaction(SIGCHLD, &old, nullptr), 0);
}
} // namespace

TEST_MAIN(int argc, char **argv, char **) {
  if (argc == 2) {
    if (LIBC_NAMESPACE::cpp::string_view(argv[1]) == "exec-child")
      return 0;
    if (LIBC_NAMESPACE::cpp::string_view(argv[1]) == "low") {
      (void)LIBC_NAMESPACE::close(0);
      (void)LIBC_NAMESPACE::close(1);
      reading("r");
      writing("we");
      return 0;
    }
    ASSERT_STREQ(argv[1], "collision");
    (void)LIBC_NAMESPACE::close(1);
    ::FILE *first = LIBC_NAMESPACE::popen("printf z", "r");
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(LIBC_NAMESPACE::fileno(first), 1);
    reading("re");
    ASSERT_EQ(LIBC_NAMESPACE::fgetc(first), 'z');
    exited(LIBC_NAMESPACE::pclose(first), 0);
    return 0;
  }
  ASSERT_TRUE(LIBC_NAMESPACE::register_pipe_fork_callbacks(on_prepare,
                                                           on_parent, nullptr));
  const char *bad_modes[] = {"", "e", "a", "r+", "rb", "wee", "er"};
  for (const char *mode : bad_modes) {
    ASSERT_EQ(LIBC_NAMESPACE::popen("exit 0", mode), nullptr);
    ASSERT_EQ(libc_errno, EINVAL);
  }
  reading("r");
  reading("re");
  writing("w");
  writing("we");
  prior_descriptors();
  // Spawn must not invoke atfork callbacks while holding the pipe registry.
  ASSERT_EQ(prepare_calls, 0);
  ASSERT_EQ(parent_calls, 0);
  low_descriptors();
  ASSERT_EQ(prepare_calls, 0);
  ASSERT_EQ(parent_calls, 0);
  inherited_registry();
  ASSERT_EQ(prepare_calls, 1);
  ASSERT_EQ(parent_calls, 1);
  interrupted_wait();
  failed_pipe();
  failed_flush();
  autoreaped_child();
  ::FILE *f = LIBC_NAMESPACE::popen("kill -TERM $$", "r");
  ASSERT_NE(f, nullptr);
  int status = LIBC_NAMESPACE::pclose(f);
  ASSERT_TRUE(WIFSIGNALED(status));
  ASSERT_EQ(WTERMSIG(status), SIGTERM);
  f = LIBC_NAMESPACE::popen("exit 127", "r");
  ASSERT_NE(f, nullptr);
  exited(LIBC_NAMESPACE::pclose(f), 127);
  return 0;
}
