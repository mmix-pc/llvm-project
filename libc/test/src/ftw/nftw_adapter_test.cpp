//===-- Tests of the ftw/nftw adapter -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Separate executable: this includes all three adapter sources with injected
// engine/I/O leaves. Do not also link ftw, nftw, walk or the FTS engine
// objects. Requires errno, OSUtil's internal exit and the unit-test framework.
// Build with -fexceptions -funwind-tables; the termination case also needs
// subprocesses. Scripted events test the adapter protocol, not the FTS
// implementation.

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/chdir.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fchdir.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/libc_errno.h"
#include "src/fts/engine.h"
#include "src/ftw/ftw.h"
#include "src/ftw/walk.h"
#include "test/UnitTest/Test.h"

#ifndef __cpp_exceptions
#error "nftw adapter ownership tests require exceptions enabled"
#endif

namespace {
using LIBC_NAMESPACE::fts::PrepareResult;
using LIBC_NAMESPACE::fts::WalkPolicy;
constexpr int XDEV = LIBC_NAMESPACE::ftw_internal::XDEV;

struct Event {
  const char *path;
  int info;
  int level;
  int device = 1;
  int error = 0;
  PrepareResult preparation = PrepareResult::Ready;
};

struct Observation {
  char path[128];
  char cwd[128];
  int type;
  int base;
  int level;
  int mode;
};

struct State {
  Event events[32];
  int count = 0;
  int next = 0;
  int opens = 0;
  int reads = 0;
  int prepares = 0;
  int skips = 0;
  int closes = 0;
  int options = 0;
  int anchor_opens = 0;
  int anchor_closes = 0;
  int fchdirs = 0;
  int chdirs = 0;
  int live_fds = 0;
  int peak_fds = 0;
  int open_error = 0;
  int read_error = 0;
  int anchor_error = 0;
  int chdir_error = 0;
  int fchdir_error = 0;
  int fail_fchdir_call = 0;
  int callbacks = 0;
  int callback_result = 0;
  int callback_error = 0;
  int stop_at = 1;
  bool throw_callback = false;
  bool away = false;
  char cwd[128] = "original";
  char path[128];
  char moved_path[128];
  FTS stream{};
  FTSENT entry{};
  struct stat status{};
  Observation seen[32];
} state;

void copy(char (&to)[128], const char *from) {
  size_t i = 0;
  do {
    if (i == sizeof(to))
      __builtin_trap();
    to[i] = from[i];
  } while (from[i++]);
}

void add(Event event) { state.events[state.count++] = event; }

int record(const char *path, const struct stat *status, int type,
           FTW *position) {
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), 0);
  Observation &seen = state.seen[state.callbacks++];
  copy(seen.path, path);
  copy(seen.cwd, state.cwd);
  seen.type = type;
  seen.base = position ? position->base : -1;
  seen.level = position ? position->level : -1;
  if (type != FTW_NS)
    seen.mode = static_cast<int>(status->st_mode);
  if (position) {
    position->base = -123;
    position->level = -456;
  }
  if (state.throw_callback)
    throw 17;
  LIBC_NAMESPACE::libc_errno = state.callback_error;
  return state.callbacks == state.stop_at ? state.callback_result : 0;
}

int record_legacy(const char *path, const struct stat *status, int type) {
  return record(path, status, type, nullptr);
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace nftw_test_fts {
using fts::PrepareResult;
using fts::WalkPolicy;

FTS *walk_open(char *const *paths, int options, __fts_compare_t compare,
               WalkPolicy policy) {
  ++state.opens;
  state.options = options;
  EXPECT_EQ(int(policy), int(WalkPolicy::Nftw));
  EXPECT_TRUE((options & FTS_NOCHDIR) != 0);
  EXPECT_FALSE((options & FTS_NOSTAT) != 0);
  EXPECT_EQ(compare, nullptr);
  EXPECT_EQ(paths[1], nullptr);
  if (state.open_error) {
    libc_errno = state.open_error;
    return nullptr;
  }
  return &state.stream;
}

FTSENT *walk_read(FTS *stream, WalkPolicy policy) {
  EXPECT_EQ(stream, &state.stream);
  EXPECT_EQ(int(policy), int(WalkPolicy::Nftw));
  EXPECT_FALSE(state.away);
  EXPECT_EQ(state.live_fds, 0);
  ++state.reads;
  if (state.next == state.count) {
    libc_errno = state.read_error;
    return nullptr;
  }
  const Event &event = state.events[state.next++];
  state.entry = {};
  state.status = {};
  state.entry.fts_info = static_cast<unsigned short>(event.info);
  state.entry.fts_level = event.level;
  state.entry.fts_errno = event.error;
  state.status.st_dev = event.device;
  state.status.st_mode =
      (event.info == FTS_SL || event.info == FTS_SLNONE) ? S_IFLNK : S_IFREG;
  // Any device/stat inspection of NS is a bug, not a comparison with device 0.
  state.entry.fts_statp =
      event.info == FTS_NS ? reinterpret_cast<struct stat *>(1) : &state.status;
  copy(state.path, event.path);
  state.entry.fts_path = state.path;
  for (const char *p = event.path; *p; ++p)
    ++state.entry.fts_pathlen;
  state.stream.fts_cur = &state.entry;
  return &state.entry;
}

PrepareResult walk_prepare_directory(FTS *stream, WalkPolicy policy) {
  EXPECT_EQ(stream, &state.stream);
  EXPECT_EQ(int(policy), int(WalkPolicy::Nftw));
  EXPECT_FALSE(state.away);
  EXPECT_EQ(state.live_fds, 0);
  ++state.prepares;
  // Model the enumeration phase's transient descriptor, closed before return.
  state.live_fds = 1;
  state.peak_fds = 1;
  state.live_fds = 0;
  const Event &event = state.events[state.next - 1];
  copy(state.moved_path, event.path);
  copy(state.path, "invalidated-path-storage");
  state.entry.fts_path = state.moved_path;
  switch (event.preparation) {
  case PrepareResult::Empty:
    state.entry.fts_info = FTS_DP;
    break;
  case PrepareResult::Unreadable:
    state.entry.fts_info = FTS_DNR;
    state.entry.fts_errno = EACCES;
    break;
  default:
    break;
  }
  libc_errno =
      event.preparation == PrepareResult::Unreadable ? EACCES : event.error;
  return event.preparation;
}

int walk_set(FTS *stream, FTSENT *entry, int instruction) {
  EXPECT_EQ(stream, &state.stream);
  EXPECT_EQ(entry, &state.entry);
  EXPECT_EQ(instruction, FTS_SKIP);
  EXPECT_EQ(int(entry->fts_info), FTS_D);
  EXPECT_FALSE(state.away);
  EXPECT_EQ(state.live_fds, 0);
  ++state.skips;
  return 0;
}

int walk_close(FTS *stream) {
  EXPECT_EQ(stream, &state.stream);
  EXPECT_EQ(state.live_fds, 0);
  ++state.closes;
  libc_errno = ERANGE; // Cleanup must not replace callback or traversal errno.
  return 0;
}
} // namespace nftw_test_fts

namespace nftw_test_syscalls {
ErrorOr<int> open(const char *path, int flags, mode_t mode) {
  EXPECT_STREQ(path, ".");
  EXPECT_EQ(flags, O_PATH | O_DIRECTORY | O_CLOEXEC);
  EXPECT_EQ(mode, mode_t(0));
  EXPECT_FALSE(state.away);
  EXPECT_EQ(state.live_fds, 0);
  ++state.anchor_opens;
  if (state.anchor_error)
    return Error(state.anchor_error);
  ++state.live_fds;
  if (state.live_fds > state.peak_fds)
    state.peak_fds = state.live_fds;
  return 123;
}

ErrorOr<int> fchdir(int fd) {
  EXPECT_EQ(fd, 123);
  EXPECT_EQ(state.live_fds, 1);
  if (++state.fchdirs == state.fail_fchdir_call)
    return Error(state.fchdir_error);
  state.away = false;
  copy(state.cwd, "original");
  return 0;
}

ErrorOr<int> chdir(const char *path) {
  EXPECT_EQ(state.live_fds, 1);
  ++state.chdirs;
  if (state.chdir_error)
    return Error(state.chdir_error);
  state.away = true;
  copy(state.cwd, path);
  return 0;
}

ErrorOr<int> close(int fd) {
  EXPECT_EQ(fd, 123);
  EXPECT_EQ(state.live_fds, 1);
  --state.live_fds;
  ++state.anchor_closes;
  return 0;
}
} // namespace nftw_test_syscalls

namespace internal {
[[noreturn]] void nftw_adapter_exit(int status) {
  // Termination must precede outer walk cleanup and consume the anchor first.
  if (state.closes || state.live_fds || !state.away)
    exit(77);
  exit(status);
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#define fts nftw_test_fts
#define linux_syscalls nftw_test_syscalls
#define exit nftw_adapter_exit
#include "src/ftw/linux/walk.cpp"
#undef exit
#undef linux_syscalls
#undef fts
#include "src/ftw/ftw.cpp"
#include "src/ftw/nftw.cpp"

class LlvmLibcNftwAdapterTest : public LIBC_NAMESPACE::testing::Test {
protected:
  void SetUp() override {
    state = {};
    LIBC_NAMESPACE::libc_errno = EDOM;
  }
  void TearDown() override { LIBC_NAMESPACE::libc_errno = 0; }
  int run(int flags = 0, int limit = 1) {
    return LIBC_NAMESPACE::nftw(state.count ? state.events[0].path : "root",
                                record, limit, flags);
  }
};

TEST_F(LlvmLibcNftwAdapterTest, InvalidArgumentsDoNotOpenTheEngine) {
  const int limits[] = {0, -1};
  for (int limit : limits) {
    EXPECT_EQ(run(0, limit), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  }
  EXPECT_EQ(run(32), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  EXPECT_EQ(run(-1), -1);
  EXPECT_EQ(state.opens, 0);
}

TEST_F(LlvmLibcNftwAdapterTest, PreparesBeforeReportingAndReloadsPath) {
  add({"root/", FTS_D, 0});
  add({"root/file", FTS_F, 1});
  add({"root/empty/", FTS_D, 1, 1, 0, PrepareResult::Empty});
  add({"root/denied", FTS_D, 1, 1, EACCES, PrepareResult::Unreadable});
  add({"root/", FTS_DP, 0});
  EXPECT_EQ(run(FTW_PHYS | FTW_CHDIR), 0);
  EXPECT_EQ(state.callbacks, 4);
  EXPECT_EQ(state.prepares, 3);
  EXPECT_EQ(state.seen[0].type, int(FTW_D));
  EXPECT_STREQ(state.seen[0].path, "root/");
  EXPECT_EQ(state.seen[0].base, 0);
  EXPECT_EQ(state.seen[2].type, int(FTW_D));
  EXPECT_EQ(state.seen[2].base, 5);
  EXPECT_EQ(state.seen[2].level, 1);
  EXPECT_EQ(state.seen[3].type, int(FTW_DNR));
  EXPECT_STREQ(state.seen[0].cwd, ".");
  EXPECT_STREQ(state.seen[1].cwd, "root/");
  EXPECT_EQ(state.peak_fds, 1);
  EXPECT_EQ(state.anchor_opens, 4);
  EXPECT_EQ(state.anchor_closes, 4);
  EXPECT_EQ(state.closes, 1);
  EXPECT_FALSE(state.away);
  EXPECT_TRUE((state.options & FTS_PHYSICAL) != 0);
  EXPECT_FALSE((state.options & FTS_LOGICAL) != 0);
}

TEST_F(LlvmLibcNftwAdapterTest, DepthUsesDirectoryCwdForEmptyAndPostorder) {
  add({"root", FTS_D, 0});
  add({"root/empty/", FTS_D, 1, 1, 0, PrepareResult::Empty});
  add({"root/denied", FTS_D, 1, 1, EACCES, PrepareResult::Unreadable});
  add({"root", FTS_DP, 0});
  EXPECT_EQ(run(FTW_DEPTH | FTW_CHDIR), 0);
  EXPECT_EQ(state.callbacks, 3);
  EXPECT_EQ(state.seen[0].type, int(FTW_DP));
  EXPECT_STREQ(state.seen[0].cwd, "root/empty/");
  EXPECT_EQ(state.seen[1].type, int(FTW_DNR));
  EXPECT_STREQ(state.seen[1].cwd, "root/");
  EXPECT_EQ(state.seen[2].type, int(FTW_DP));
  EXPECT_STREQ(state.seen[2].cwd, "root");
  EXPECT_EQ(state.peak_fds, 1);
}

TEST_F(LlvmLibcNftwAdapterTest, MountAndXdevPruneBeforePreparation) {
  const int modes[] = {XDEV, FTW_MOUNT, XDEV | FTW_MOUNT};
  const int depths[] = {0, FTW_DEPTH};
  for (int flags : modes) {
    for (int depth : depths) {
      state = {};
      add({"root", FTS_D, 0});
      add({"root/foreign", FTS_D, 1, 2});
      add({"root/foreign", FTS_DP, 1, 2});
      add({"root/foreign-file", FTS_F, 1, 2});
      add({"root/unknown", FTS_NS, 1, 0, EACCES});
      add({"root", FTS_DP, 0});
      EXPECT_EQ(run(flags | depth), 0);
      EXPECT_EQ(state.prepares, 1);
      EXPECT_EQ(state.skips, 1);
      EXPECT_TRUE((state.options & FTS_XDEV) != 0);
      EXPECT_EQ(state.callbacks, flags & FTW_MOUNT ? 2 : 4);
      int unknown = depth ? state.callbacks - 2 : state.callbacks - 1;
      EXPECT_EQ(state.seen[unknown].type, int(FTW_NS));
      if (!(flags & FTW_MOUNT))
        EXPECT_EQ(state.seen[depth ? 0 : 1].type,
                  int(depth ? FTW_DP : FTW_D));
      EXPECT_EQ(state.anchor_opens, 0);
    }
  }
}

TEST_F(LlvmLibcNftwAdapterTest, LogicalCyclesAndTraditionalFtwClassification) {
  const int depths[] = {0, FTW_DEPTH};
  for (int depth : depths) {
    state = {};
    add({"root", FTS_D, 0});
    add({"root/cycle", FTS_DC, 1});
    add({"root/dangling", FTS_SLNONE, 1});
    add({"root", FTS_DP, 0});
    EXPECT_EQ(run(depth), 0);
    EXPECT_TRUE((state.options & FTS_LOGICAL) != 0);
    EXPECT_EQ(state.prepares, 1);
    EXPECT_EQ(state.callbacks, depth ? 2 : 3);
    EXPECT_EQ(state.seen[depth ? 0 : 2].type, int(FTW_SLN));
    EXPECT_EQ(state.seen[depth ? 0 : 2].mode, int(S_IFLNK));
  }
  state = {};
  add({"root", FTS_D, 0});
  add({"root/dangling", FTS_SLNONE, 1});
  add({"root", FTS_DP, 0});
  EXPECT_EQ(LIBC_NAMESPACE::ftw("root", record_legacy, 1), 0);
  EXPECT_EQ(state.callbacks, 2);
  EXPECT_EQ(state.seen[0].type, int(FTW_D));
  EXPECT_EQ(state.seen[1].type, int(FTW_NS));
  EXPECT_TRUE((state.options & FTS_LOGICAL) != 0);
  EXPECT_EQ(state.anchor_opens, 0);
}

TEST_F(LlvmLibcNftwAdapterTest, RootAccessDenialDoesNotInvokeCallback) {
  add({"root", FTS_NS, 0, 0, EACCES});
  EXPECT_EQ(run(), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EACCES);
  EXPECT_EQ(state.callbacks, 0);
  EXPECT_EQ(state.closes, 1);
  state = {};
  add({"root", FTS_D, 0, 1, EACCES, PrepareResult::Unreadable});
  EXPECT_EQ(run(), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EACCES);
  EXPECT_EQ(state.callbacks, 0);
  EXPECT_EQ(state.closes, 1);
}

TEST_F(LlvmLibcNftwAdapterTest, FatalOpenReadAndPreparationErrorsArePreserved) {
  const int errors[] = {ENOENT, ENOTDIR, ELOOP, EIO, ENOMEM, EMFILE, EOVERFLOW};
  for (int error : errors) {
    state = {};
    state.open_error = error;
    EXPECT_EQ(run(), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.closes, 0);
    state = {};
    state.read_error = error;
    EXPECT_EQ(run(), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.closes, 1);
    state = {};
    add({"root", FTS_D, 0, 1, error, PrepareResult::Error});
    EXPECT_EQ(run(FTW_CHDIR), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.callbacks, 0);
    EXPECT_EQ(state.anchor_opens, 0);
    EXPECT_EQ(state.closes, 1);
  }
}

TEST_F(LlvmLibcNftwAdapterTest, NonzeroCallbackReturnAndErrnoPrecedence) {
  const int results[] = {-7, -1, 1, 2, 37};
  const int errors[] = {0, EACCES, ENOSPC};
  for (int result : results) {
    for (int error : errors) {
      state = {};
      add({"root", FTS_D, 0});
      add({"root/never", FTS_F, 1});
      state.callback_result = result;
      state.callback_error = error;
      EXPECT_EQ(run(FTW_CHDIR), result);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno),
                result == -1 && !error ? EACCES : error);
      EXPECT_EQ(state.callbacks, 1);
      EXPECT_EQ(state.reads, 1);
      EXPECT_EQ(state.anchor_closes, 1);
      EXPECT_EQ(state.closes, 1);
      EXPECT_FALSE(state.away);
    }
  }
}

TEST_F(LlvmLibcNftwAdapterTest, CompletionPreservesLastCallbackErrno) {
  add({"root", FTS_F, 0});
  state.callback_error = ENOSPC;
  EXPECT_EQ(run(), 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOSPC);
  EXPECT_EQ(state.closes, 1);
}

TEST_F(LlvmLibcNftwAdapterTest, CwdSetupFailureDoesNotInvokeCallback) {
  const int stages[] = {0, 1, 2};
  for (int stage : stages) {
    state = {};
    add({"root", FTS_F, 0});
    if (stage == 0)
      state.anchor_error = EMFILE;
    else if (stage == 1) {
      state.fail_fchdir_call = 1;
      state.fchdir_error = EACCES;
    } else
      state.chdir_error = ENOTDIR;
    EXPECT_EQ(run(FTW_CHDIR), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno),
              stage == 0 ? EMFILE : (stage == 1 ? EACCES : ENOTDIR));
    EXPECT_EQ(state.callbacks, 0);
    EXPECT_EQ(state.anchor_closes, stage == 0 ? 0 : 1);
    EXPECT_EQ(state.closes, 1);
    EXPECT_EQ(state.live_fds, 0);
    EXPECT_FALSE(state.away);
  }
}

TEST_F(LlvmLibcNftwAdapterTest,
       RestoreFailureOverridesCallbackAndStopsWalking) {
  const int results[] = {0, -1, 37};
  for (int result : results) {
    state = {};
    add({"root", FTS_F, 0});
    add({"never", FTS_F, 1});
    state.callback_result = result;
    state.callback_error = ENOSPC;
    state.fail_fchdir_call = 2;
    state.fchdir_error = EIO;
    EXPECT_EQ(run(FTW_CHDIR), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EIO);
    EXPECT_EQ(state.callbacks, 1);
    EXPECT_EQ(state.reads, 1);
    EXPECT_EQ(state.fchdirs, 2);
    EXPECT_EQ(state.anchor_closes, 1);
    EXPECT_EQ(state.closes, 1);
    EXPECT_TRUE(state.away);
  }
}

TEST_F(LlvmLibcNftwAdapterTest, PathsAndCallbackRecordMutationStayLocal) {
  struct Case {
    const char *path;
    int base;
    const char *parent;
  } cases[] = {{".", 0, "."},
               {"..", 0, "."},
               {"/", 1, "/"},
               {"////", 4, "////"},
               {"dir///", 0, "."},
               {"//parent///dir/", 11, "//parent///"},
               {"link/../dir", 8, "link/../"}};
  for (const Case &item : cases) {
    state = {};
    add({item.path, FTS_D, 0, 1, 0, PrepareResult::Empty});
    EXPECT_EQ(run(FTW_CHDIR), 0);
    EXPECT_EQ(state.callbacks, 1);
    EXPECT_STREQ(state.seen[0].path, item.path);
    EXPECT_EQ(state.seen[0].base, item.base);
    EXPECT_EQ(state.seen[0].level, 0);
    EXPECT_STREQ(state.seen[0].cwd, item.parent);
    EXPECT_EQ(state.entry.fts_level, 0);
  }
}

TEST_F(LlvmLibcNftwAdapterTest, CallbackExceptionRestoresBeforeWalkCleanup) {
  add({"root", FTS_D, 0});
  state.throw_callback = true;
  bool caught = false;
  try {
    run(FTW_CHDIR);
  } catch (int value) {
    EXPECT_EQ(value, 17);
    caught = true;
  }
  EXPECT_TRUE(caught);
  EXPECT_FALSE(state.away);
  EXPECT_EQ(state.anchor_closes, 1);
  EXPECT_EQ(state.closes, 1);
  EXPECT_EQ(state.live_fds, 0);
}

#if LIBC_TEST_SUBPROCESS_TESTS
TEST_F(LlvmLibcNftwAdapterTest, RestoreFailureDuringUnwindTerminates) {
  EXPECT_EXITS(
      [] {
        state = {};
        add({"root", FTS_F, 0});
        state.throw_callback = true;
        state.fail_fchdir_call = 2;
        state.fchdir_error = EACCES;
        try {
          LIBC_NAMESPACE::nftw("root", record, 1, FTW_CHDIR);
        } catch (...) {
          // Propagating the original callback exception is not termination.
        }
      },
      127);
}
#endif
