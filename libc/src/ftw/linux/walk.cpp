//===-- Linux ftw/nftw adapter for the private FTS engine ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ftw/walk.h"

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/chdir.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fchdir.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/libc_errno.h"
#include "src/fts/engine.h"

namespace LIBC_NAMESPACE_DECL {
namespace ftw_internal {
namespace {

constexpr auto POLICY = fts::WalkPolicy::Nftw;

struct WalkOwner {
  FTS *stream;
  ~WalkOwner() {
    int saved = libc_errno;
    // NOCHDIR close only releases memory; it cannot attempt cwd recovery.
    fts::walk_close(stream);
    libc_errno = saved;
  }
};

class CallbackCwd {
  int fd = -1;
  bool changed = false;

  void close() {
    if (fd >= 0) {
      // Linux consumes the descriptor even on a close error. Never retry it.
      (void)linux_syscalls::close(fd);
      fd = -1;
    }
  }

public:
  CallbackCwd() = default;
  CallbackCwd(const CallbackCwd &) = delete;
  CallbackCwd &operator=(const CallbackCwd &) = delete;

  int enter(const char *directory) {
    auto opened =
        linux_syscalls::open(".", O_PATH | O_DIRECTORY | O_CLOEXEC, 0);
    if (!opened)
      return opened.error();
    fd = opened.value();
    // Detect initially missing search permission before leaving the cwd.
    auto accessible = linux_syscalls::fchdir(fd);
    if (!accessible) {
      close();
      return accessible.error();
    }
    auto entered = linux_syscalls::chdir(directory);
    if (!entered) {
      close();
      return entered.error();
    }
    changed = true;
    return 0;
  }

  int finish() {
    int error = 0;
    if (changed) {
      auto restored = linux_syscalls::fchdir(fd);
      if (!restored)
        error = restored.error();
      changed = false;
    }
    close();
    return error;
  }

  ~CallbackCwd() {
    int saved = libc_errno;
    // Normal paths call finish explicitly. Here a callback is unwinding, so
    // failed recovery must not continue unwinding under a falsely restored cwd.
    if (finish())
      internal::exit(127);
    libc_errno = saved;
  }
};

int fail(int error) {
  libc_errno = error;
  return -1;
}

size_t filename_offset(const char *path, size_t length) {
  size_t end = length;
  while (end && path[end - 1] == '/')
    --end;
  // An all-slash root has no filename component.
  if (!end)
    return length;
  while (end && path[end - 1] != '/')
    --end;
  return end;
}

int report(FTSENT *entry, int type, Callback callback, int flags) {
  char *path = entry->fts_path;
  size_t base = filename_offset(path, entry->fts_pathlen);
  if (base > static_cast<size_t>(cpp::numeric_limits<int>::max()) ||
      entry->fts_level < 0)
    return fail(EOVERFLOW);
  FTW position{static_cast<int>(base), entry->fts_level};

  CallbackCwd cwd;
  if (flags & FTW_CHDIR) {
    int error;
    if (type == FTW_DP || base == entry->fts_pathlen) {
      error = cwd.enter(path);
    } else if (!base) {
      error = cwd.enter(".");
    } else {
      // Preserve symlink/.. and trailing-slash resolution. Only the transient
      // parent pathname passed to chdir is shortened, never the callback path.
      char saved = path[base];
      path[base] = '\0';
      error = cwd.enter(path);
      path[base] = saved;
    }
    if (error)
      return fail(error);
  }

  libc_errno = 0;
  int result = callback(path, entry->fts_statp, type, &position);
  int callback_error = libc_errno;
  int restore_error = cwd.finish();
  if (restore_error)
    return fail(restore_error);
  libc_errno = result == -1 && callback_error == 0 ? EACCES : callback_error;
  return result;
}

} // namespace

int walk(const char *path, Callback callback, int fd_limit, int flags) {
  constexpr int VALID_FLAGS =
      FTW_PHYS | FTW_MOUNT | FTW_CHDIR | FTW_DEPTH | XDEV;
  if (fd_limit <= 0 || (flags & ~VALID_FLAGS))
    return fail(EINVAL);

  // Enumeration closes its one fd before each event. Only then may report()
  // open its cwd anchor; every positive fd_limit is therefore sufficient.
  int options = FTS_NOCHDIR | ((flags & FTW_PHYS) ? FTS_PHYSICAL : FTS_LOGICAL);
  if (flags & (FTW_MOUNT | XDEV))
    options |= FTS_XDEV;
  char *paths[] = {const_cast<char *>(path), nullptr};
  FTS *stream = fts::walk_open(paths, options, nullptr, POLICY);
  if (!stream)
    return -1;
  WalkOwner owner{stream};
  dev_t root_device = 0;
  int last_callback_errno = 0;

  for (;;) {
    libc_errno = 0;
    FTSENT *entry = fts::walk_read(stream, POLICY);
    if (!entry) {
      if (libc_errno)
        return -1;
      libc_errno = last_callback_errno;
      return 0;
    }
    int info = entry->fts_info;
    bool root = entry->fts_level == FTS_ROOTLEVEL;
    // The starting path's search/read denial is a required EACCES failure,
    // distinct from the descendant DNR/NS callback cases.
    if (info == FTS_ERR || ((info == FTS_NS || info == FTS_DNR) &&
                            (root || entry->fts_errno != EACCES)))
      return fail(entry->fts_errno);

    // NS has undefined metadata; it cannot establish a device boundary.
    bool known_device = info != FTS_NS;
    if (root && known_device)
      root_device = entry->fts_statp->st_dev;
    bool foreign = known_device && entry->fts_statp->st_dev != root_device;
    if (info == FTS_D && foreign && (flags & (FTW_MOUNT | XDEV))) {
      if (fts::walk_set(stream, entry, FTS_SKIP))
        return -1;
    }
    if (foreign && (flags & FTW_MOUNT))
      continue;

    int type;
    switch (info) {
    case FTS_D:
      if (foreign && (flags & XDEV)) {
        if (flags & FTW_DEPTH)
          continue; // read will return the skipped directory's DP event.
        type = FTW_D;
        break;
      }
      switch (fts::walk_prepare_directory(stream, POLICY)) {
      case fts::PrepareResult::Error:
        return -1;
      case fts::PrepareResult::Unreadable:
        if (root)
          return fail(entry->fts_errno);
        type = FTW_DNR;
        break;
      case fts::PrepareResult::Ready:
        if (flags & FTW_DEPTH)
          continue;
        type = FTW_D;
        break;
      case fts::PrepareResult::Empty:
        type = flags & FTW_DEPTH ? FTW_DP : FTW_D;
        break;
      }
      break;
    case FTS_DP:
      if (!(flags & FTW_DEPTH))
        continue;
      type = FTW_DP;
      break;
    case FTS_DC:
      // This is an ancestor cycle, not a global visited-inode filter. Logical
      // preorder reports the edge only; depth suppresses it altogether.
      if (flags & FTW_DEPTH)
        continue;
      type = FTW_D;
      break;
    case FTS_DNR:
      type = FTW_DNR;
      break;
    case FTS_NS:
      type = FTW_NS;
      break;
    case FTS_F:
    case FTS_DEFAULT:
      type = FTW_F;
      break;
    case FTS_SL:
      type = FTW_SL;
      break;
    case FTS_SLNONE:
      type = FTW_SLN;
      break;
    default:
      return fail(EIO);
    }
    // Preparation may have moved path storage. report reads entry afresh.
    int result = report(entry, type, callback, flags);
    last_callback_errno = libc_errno;
    if (result)
      return result;
  }
}

} // namespace ftw_internal
} // namespace LIBC_NAMESPACE_DECL
