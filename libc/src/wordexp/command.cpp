//===-- Word substitution command capture ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/command.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fcntl.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/spawn/posix_spawn.h"
#include "src/spawn/posix_spawn_file_actions_addclose.h"
#include "src/spawn/posix_spawn_file_actions_adddup2.h"
#include "src/spawn/posix_spawn_file_actions_addopen.h"
#include "src/spawn/posix_spawn_file_actions_destroy.h"
#include "src/spawn/posix_spawn_file_actions_init.h"
#include "src/stdio/linux/pipe_process.h"
#include "src/unistd/read.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {

struct Descriptor {
  PipeProcess &process;
  int fd = -1;
  void close() {
    if (fd >= 0) {
      process.disable();
      // Linux consumes close even on EINTR. Never retry a potentially reused
      // fd.
      (void)linux_syscalls::close(fd);
      fd = -1;
    }
  }
  ~Descriptor() { close(); }
};

struct Actions {
  posix_spawn_file_actions_t value;
  bool initialized = false;
  void clear() {
    if (initialized) {
      initialized = false;
      (void)LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&value);
    }
  }
  ~Actions() { clear(); }
};
} // namespace

ErrorOr<int> capture_word_command(const char *command, char *const *environment,
                                  bool show_error, CommandOutput output) {
  if (!command || !environment || !output.append)
    return Error(EINVAL);
  // Descriptor owners unwind before the process owner: close the pipe before
  // reaping, including allocation failure or forced unwinding during read.
  PipeProcess process;
  Descriptor reader{process}, writer{process};
  int fds[2];
  long created = syscall_impl<long>(SYS_pipe2, fds, O_CLOEXEC);
  if (created < 0)
    return Error(static_cast<int>(-created));
  reader.fd = fds[0];
  writer.fd = fds[1];
  if (writer.fd <= 2) {
    // Avoid dup2(fd, fd), which need not remove CLOEXEC in the spawn provider,
    // and keep stderr replacement independent of the pipe's original number.
    auto copied = linux_syscalls::fcntl(writer.fd, F_DUPFD_CLOEXEC,
                                        reinterpret_cast<void *>(3));
    if (!copied)
      return Error(copied.error());
    writer.close();
    writer.fd = *copied;
  }
  Actions actions;
  int error = LIBC_NAMESPACE::posix_spawn_file_actions_init(&actions.value);
  if (error)
    return Error(error);
  actions.initialized = true;
  error = LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions.value,
                                                            reader.fd);
  if (!error)
    error = LIBC_NAMESPACE::posix_spawn_file_actions_adddup2(&actions.value,
                                                             writer.fd, 1);
  if (!error)
    error = LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions.value,
                                                              writer.fd);
  if (!error && !show_error)
    error = LIBC_NAMESPACE::posix_spawn_file_actions_addopen(
        &actions.value, 2, "/dev/null", O_WRONLY, 0);
  if (error)
    return Error(error);
  char *args[] = {const_cast<char *>("sh"), const_cast<char *>("-c"),
                  const_cast<char *>("--"), const_cast<char *>(command),
                  nullptr};
  error = LIBC_NAMESPACE::posix_spawn(&process.pid, "/bin/sh", &actions.value,
                                      nullptr, args, environment);
  // Retire action allocations before restoring cancellation, including the
  // caller's original asynchronous type at successful return.
  actions.clear();
  if (error)
    return Error(error);
  writer.close();
  for (;;) {
    char buffer[4096];
    process.resume();
    ssize_t count = LIBC_NAMESPACE::read(reader.fd, buffer, sizeof(buffer));
    int read_error = count < 0 ? int(libc_errno) : 0;
    process.disable();
    if (count < 0) {
      if (read_error == EINTR)
        continue;
      return Error(read_error);
    }
    if (!count)
      break;
    if (int sink_error = output.append(output.context, {buffer, size_t(count)}))
      return Error(sink_error);
  }
  reader.close();
  int status;
  if (int wait_error = process.wait(status))
    return Error(wait_error);
  process.restore();
  return status;
}

ErrorOr<int> capture_word_command(const WordSource &source, WordToken token,
                                  RestartableConverter converter,
                                  char *const *environment, bool show_error,
                                  CommandOutput output) {
  if (token.kind != WordToken::Kind::COMMAND &&
      token.kind != WordToken::Kind::BACKQUOTED_COMMAND)
    return Error(EINVAL);
  CharVector body;
  int error = source.command(token.begin, token.end,
                             token.kind == WordToken::Kind::BACKQUOTED_COMMAND,
                             token.quoted, converter, body);
  if (error)
    return Error(error);
  return capture_word_command(body.c_str(), environment, show_error, output);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
