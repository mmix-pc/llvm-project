//===-- Linux pipe streams ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/stdio/linux/pipe_file.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/File/linux/file.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fcntl.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/raw_mutex.h"
#include "src/spawn/posix_spawn.h"
#include "src/spawn/posix_spawn_file_actions_addclose.h"
#include "src/spawn/posix_spawn_file_actions_adddup2.h"
#include "src/spawn/posix_spawn_file_actions_destroy.h"
#include "src/stdio/linux/pipe_fork.h"
#include "src/stdio/linux/pipe_process.h"
#include "src/unistd/environ.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
// Linux UAPI command, also used internally by src/fcntl/linux/fcntl.cpp.
// Public feature-test macros must not control this implementation detail.
constexpr int DUPFD_CLOEXEC = 1030;
int retire_pipe(File *);

struct PipeFile : LinuxFile {
  uint8_t buffer[DEFAULT_BUFFER_SIZE];
  pid_t child = -1;
  PipeFile *next = nullptr;
  bool registered = false;
  bool abort_close = false;
  PipeFile **close_owner = nullptr;
  int *wait_status = nullptr;

  explicit PipeFile(bool read)
      : LinuxFile(-1, buffer, sizeof(buffer), _IOFBF, false,
                  ModeFlags(read ? OpenMode::READ : OpenMode::WRITE), false,
                  retire_pipe) {}
};

RawMutex registry_lock;
PipeFile *pipes = nullptr;
bool fork_registered = false;

void before_fork() { registry_lock.lock(); }
void after_fork_parent() { registry_lock.unlock(); }
void after_fork_child() {
  // Descriptors are inherited, children are not. Keep the descriptor list for
  // future popen close actions, but never signal/reap a parent's subprocess.
  for (PipeFile *p = pipes; p; p = p->next) {
    p->child = -1;
    p->close_owner = nullptr;
    p->wait_status = nullptr;
    p->abort_close = false;
  }
  registry_lock.unlock();
}

struct StreamOwner {
  PipeProcess &policy;
  PipeFile *file;
  ~StreamOwner() {
    if (file) {
      int error = libc_errno;
      policy.disable();
      file->abort_close = true;
      (void)file->close(false);
      libc_errno = error;
    }
  }
};

struct Descriptor {
  int fd = -1;
  ~Descriptor() {
    if (fd >= 0)
      (void)linux_syscalls::close(fd);
  }
};

struct Actions {
  posix_spawn_file_actions_t value{};
  ~Actions() { (void)LIBC_NAMESPACE::posix_spawn_file_actions_destroy(&value); }
};

int retire_pipe(File *base) {
  PipeProcess process;
  auto *file = static_cast<PipeFile *>(base);
  // Transfer ownership before any cancelable wait, including the caller's
  // cleanup pointer: after deletion it must no longer reference this stream.
  if (file->close_owner)
    *file->close_owner = nullptr;
  process.pid = file->child;
  int *status_out = file->wait_status;
  bool abort = file->abort_close;
  int close_error = 0;
  {
    cpp::lock_guard lock(registry_lock);
    if (file->registered) {
      PipeFile **link = &pipes;
      while (*link != file)
        link = &(*link)->next;
      *link = file->next;
    }
    // Removal and descriptor consumption are one transaction with spawning.
    if (file->get_fd() >= 0) {
      auto closed = linux_syscalls::close(file->get_fd());
      // Linux consumes the descriptor even on EINTR. Do not retry close or
      // expose EINTR from pclose after the child has been successfully waited.
      if (!closed && closed.error() != EINTR)
        close_error = closed.error();
    }
  }
  delete file;
  if (abort)
    return close_error; // Process owner reaps without cancellation.
  int status = 0;
  int error = process.wait(status);
  if (!error && status_out)
    *status_out = status;
  return error ? error : close_error;
}
} // namespace

File *open_pipe(const char *command, const char *mode) {
  if (!command || !mode || (mode[0] != 'r' && mode[0] != 'w') ||
      (mode[1] && (mode[1] != 'e' || mode[2]))) {
    libc_errno = EINVAL;
    return nullptr;
  }
  bool read = mode[0] == 'r', cloexec = mode[1] == 'e';
  PipeProcess policy;
  AllocChecker ac;
  auto *file = new (ac) PipeFile(read);
  if (!ac) {
    libc_errno = ENOMEM;
    return nullptr;
  }
  StreamOwner owner{policy, file};
  file->close_owner = &owner.file;
  Descriptor child_end;
  int error = 0;
  {
    cpp::lock_guard lock(registry_lock);
    if (!fork_registered) {
      if (!register_pipe_fork_callbacks(before_fork, after_fork_parent,
                                        after_fork_child)) {
        libc_errno = ENOMEM;
        return nullptr;
      }
      fork_registered = true;
    }
    int fds[2];
    long result = syscall_impl<long>(SYS_pipe2, fds, O_CLOEXEC);
    if (result < 0) {
      libc_errno = static_cast<int>(-result);
      return nullptr;
    }
    file->set_fd(fds[read ? 0 : 1]);
    child_end.fd = fds[read ? 1 : 0];
    int target = read ? 1 : 0;
    if (child_end.fd == target) {
      // dup2(fd, fd) does not clear CLOEXEC in the generic spawn provider.
      auto copied = linux_syscalls::fcntl(child_end.fd, DUPFD_CLOEXEC,
                                          reinterpret_cast<void *>(3));
      if (!copied) {
        libc_errno = copied.error();
        return nullptr;
      }
      (void)linux_syscalls::close(child_end.fd);
      child_end.fd = *copied;
    }
    Actions actions;
    // Close old pipes before dup2: one of them may occupy stdin/stdout.
    for (PipeFile *p = pipes; p && !error; p = p->next)
      error = LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions.value,
                                                                p->get_fd());
    if (!error)
      error = LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions.value,
                                                                file->get_fd());
    if (!error)
      error = LIBC_NAMESPACE::posix_spawn_file_actions_adddup2(
          &actions.value, child_end.fd, target);
    if (!error)
      error = LIBC_NAMESPACE::posix_spawn_file_actions_addclose(&actions.value,
                                                                child_end.fd);
    char *args[] = {const_cast<char *>("sh"), const_cast<char *>("-c"),
                    const_cast<char *>("--"), const_cast<char *>(command),
                    nullptr};
    if (!error)
      // Both selected spawn providers clone directly, without atfork
      // callbacks. Replacing this with public fork would recurse on this lock.
      error =
          LIBC_NAMESPACE::posix_spawn(&file->child, "/bin/sh", &actions.value,
                                      nullptr, args, LIBC_NAMESPACE::environ);
    if (!error && !cloexec) {
      auto flags = linux_syscalls::fcntl(file->get_fd(), F_SETFD, nullptr);
      if (!flags)
        error = flags.error();
    }
    if (!error) {
      file->next = pipes;
      pipes = file;
      file->registered = true;
      File::add_file(file);
    }
  }
  if (error) {
    libc_errno = error;
    return nullptr;
  }
  // Restore while cleanup still owns the unpublished return value.
  policy.restore();
  file->close_owner = nullptr;
  owner.file = nullptr;
  return file;
}

int close_pipe(File *base) {
  PipeProcess policy;
  PipeFile *file = nullptr;
  {
    cpp::lock_guard lock(registry_lock);
    for (PipeFile *p = pipes; p; p = p->next)
      if (static_cast<File *>(p) == base) {
        file = p;
        break;
      }
  }
  if (!file) {
    libc_errno = EINVAL;
    return -1;
  }
  StreamOwner owner{policy, file};
  int status = 0;
  file->close_owner = &owner.file;
  file->wait_status = &status;
  policy.resume();
  int flush_error;
  do {
    flush_error = file->flush();
  } while (flush_error == EINTR);
  int close_error = file->close(false);
  int error = flush_error ? flush_error : close_error;
  if (error) {
    libc_errno = error;
    return -1;
  }
  return status;
}
} // namespace LIBC_NAMESPACE_DECL
