# File Hierarchy Traversal

`engine.cpp` and `include/llvm-libc-types/FTS.h` derive from NetBSD's
`lib/libc/gen/fts.c` and `include/fts.h`, at commit
`55cb7200ca75e34d35427835b5e96e3d201d694d`.

Upstream: <https://github.com/NetBSD/src>

Original SHA-256:

| File | SHA-256 |
| --- | --- |
| `fts.c` | `24157b290edfbbdb9fa63e5c05de7632b7427fae1eb465deba4d6af1b21be55c` |
| `fts.h` | `8d2d89cefd95f03004a09f77d3b3c0c11dffd1b804379a3010972cf9edb8fa62` |

The original BSD three-clause notices remain in both derived files. New libc
adapters retain the LLVM license. No glibc or Gnulib implementation was copied.

## Adaptation

The linked traversal state machine, directory pre/postorder events, ancestor
cycle detection and instruction processing are retained. The port uses libc's
namespace, errno, allocation and sorting services. NetBSD symbol aliases,
compatibility layouts, whiteouts and platform namespace machinery are removed.
Lengths use `size_t`; the public layout is libc-owned, not a binary promise to
match NetBSD, FreeBSD or glibc. Public headers expose the five traditional
entrypoints, with exactly one of PHYSICAL and LOGICAL required at open.

Linux I/O uses private, non-cancelling open/getdents/close leaves. Every internal
descriptor is opened atomically with CLOEXEC; close errors never trigger a
retry. The directory reader validates kernel record boundaries and names.
No directory-link-count optimization is assumed. NOSTAT skips known
non-directories using d_type, but still examines unknown directory entries.

Local repairs cover path rebasing before stat after realloc, child-list
instructions, repeated children calls, early-close descriptor ownership,
unrecoverable-error tree reachability, directory read errors, allocation
overflow and sorting failure. Allocation failure does not silently discard
the requested sort order.

## Lifetime And Concurrency

Entry and path storage belongs to the stream. Copy any data needed beyond the
next traversal operation; directory ancestors remain available during descent.
Children lists are invalidated by the next children/read/close operation.
The shared path buffer can change during children queries. The comparator
must return normally and must not mutate or recursively operate on its stream.

Independent NOCHDIR streams may be used concurrently. Other modes may change
process cwd and require external process-wide exclusion. A stream itself
requires single-owner access. LOGICAL implies NOCHDIR. Traversal is neither
signal-safe nor asynchronous-cancellation-safe; its internal I/O does not
introduce deferred cancellation points. Callers retain responsibility for
closing the stream when abandoning a walk.

Normal end returns null with errno zero. Per-entry failures use FTS_NS,
FTS_DNR or FTS_ERR and fts_errno; fatal failures return null with errno and
remain closeable. SKIP on a returned directory yields its postorder event;
SKIP on a pending child prevents its visit. Physical traversal does not
promise a security snapshot against concurrent filesystem replacement.
NOCHDIR path resolution remains subject to the kernel's pathname limit.
