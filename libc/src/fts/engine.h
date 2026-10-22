//===-- Internal FTS traversal engine ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_FTS_ENGINE_H
#define LLVM_LIBC_SRC_FTS_ENGINE_H
#include "include/llvm-libc-types/FTS.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace fts {
// Policy is private and is not stored in the public FTS layout or option bits.
// Use the same policy for open, read and prepare throughout a stream's lifetime.
// Nftw requires NOCHDIR (also implied by LOGICAL), full stat data and no SEEDOT.
// Do not mix it with walk_children. Comparators must still return normally.
enum class WalkPolicy { Fts, Nftw };

// Nftw preserves EACCES as FTS_NS, but other stat errors stop the walk. Only
// ENOENT followed by successful lstat of a symlink becomes FTS_SLNONE; its stat
// buffer describes the link. ENOTDIR/ELOOP and other target errors remain errors.
// A fatal root stat error fails open; a root FTS_NS still needs adapter handling.
// Directory open errors other than EACCES also stop a Nftw walk; EACCES yields
// FTS_DNR. Fatal read returns null with errno and FTS_STOP; capture that errno
// and close. Normal exhaustion returns null with errno zero. Without prepare,
// read still yields FTS_D before discovering readability on the following read.
FTS *walk_open(char *const *, int, __fts_compare_t,
              WalkPolicy = WalkPolicy::Fts);
FTSENT *walk_read(FTS *, WalkPolicy = WalkPolicy::Fts);

enum class PrepareResult { Ready, Empty, Unreadable, Error };

// Prepare the current, unprepared FTS_D before reporting it. Requires NOCHDIR,
// full stat data, no pending children/NAMEONLY/instruction, and no XDEV pruning
// of this node. The adapter must decide mount/cycle pruning before calling.
// No nftw callback is invoked; at return no directory descriptor remains open.
//
// Ready: current remains FTS_D; the stream owns a nonempty child list, consumed
// by the next read. Empty: current becomes FTS_DP; report it once (D or DP as
// appropriate), then read advances PAST it without returning a second event.
// Unreadable: current becomes FTS_DNR with EACCES; read advances past it without
// descendants or DP. Error: errno describes failure; I/O/allocation/stat errors
// leave FTS_STOP and a closeable stream, while invalid state returns EINVAL
// without mutation. Repeat preparation is invalid, including after Empty.
//
// The current entry survives but path storage may move. On every valid call,
// its original pathname is restored, including a trailing slash. Reload path
// pointers after the call. Ready/Empty set errno to zero; other outcomes set it.
PrepareResult walk_prepare_directory(FTS *, WalkPolicy);

// The adapter still owns root/descendant error distinctions, FTW classification,
// MOUNT reporting versus XDEV pruning, cycle reporting, base/level, callback
// stop/errno, and walk ownership across exceptions/cancellation. Select one root
// and no comparator. The adapter owns fd_limit and CLOEXEC for its cwd anchor;
// restore original cwd before any engine call so that anchor cannot overlap
// enumeration. Cwd restoration failures return the actual error normally and
// terminate during unwind; neither callback/cwd policy is implemented here.
FTSENT *walk_children(FTS *, int);
int walk_set(FTS *, FTSENT *, int);
int walk_close(FTS *);
} // namespace fts
} // namespace LIBC_NAMESPACE_DECL
#endif
