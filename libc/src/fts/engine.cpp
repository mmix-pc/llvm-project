/*	$NetBSD: fts.c,v 1.52 2022/04/19 20:32:15 rillig Exp $	*/

/*-
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "engine.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "hdr/func/realloc.h"
#include "hdr/limits_macros.h"
#include "hdr/sys_stat_macros.h"
#include "linux_io.h"
#include "src/__support/libc_assert.h"
#include "src/__support/libc_errno.h"
#include "src/stdlib/qsort_util.h"
#include "src/string/memmove.h"
#include "src/string/memset.h"
#include "src/string/strlen.h"
#include "src/string/strrchr.h"

namespace LIBC_NAMESPACE_DECL {
namespace fts {
#define _DIAGASSERT LIBC_ASSERT
#ifdef errno
#undef errno
#endif
#define errno libc_errno
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MAXPATHLEN 1024
#define __fts_stat_t struct stat
#define __fts_ino_t ino_t
static FTSENT *fts_alloc(FTS *, const char *, size_t);
static FTSENT *fts_build(FTS *, int, WalkPolicy);
static void fts_free(FTSENT *);
static void fts_lfree(FTSENT *);
static void fts_load(FTS *, FTSENT *);
static size_t fts_maxarglen(char *const *);
static size_t fts_pow2(size_t);
static int fts_palloc(FTS *, size_t);
static void fts_padjust(FTS *, FTSENT *);
static FTSENT *fts_sort(FTS *, FTSENT *, size_t);
static unsigned short fts_stat(FTS *, FTSENT *, int, WalkPolicy);
static int fts_safe_changedir(const FTS *, const FTSENT *, int, const char *);

#define ISDOT(a) (a[0] == '.' && (!a[1] || (a[1] == '.' && !a[2])))

#define CLR(opt) (sp->fts_options &= ~(opt))
#define ISSET(opt) (sp->fts_options & (opt))
#define SET(opt) (sp->fts_options |= (opt))

#define FCHDIR(sp, fd) (!ISSET(FTS_NOCHDIR) && fchdir(fd))

/* fts_build flags */
#define BCHILD 1 /* fts_children */
#define BNAMES 2 /* fts_children, names only */
#define BREAD 3  /* fts_read */

static bool nftw_options(int options) {
  return (options & (FTS_NOCHDIR | FTS_LOGICAL)) &&
         !(options & (FTS_NOSTAT | FTS_SEEDOT));
}

FTS *walk_open(char *const *argv, int options,
              int (*compar)(const FTSENT **, const FTSENT **),
              WalkPolicy policy) {
  FTS *sp;
  FTSENT *p, *root;
  size_t nitems;
  FTSENT *parent, *tmp = NULL; /* pacify gcc */
  size_t len;

  /* Options check. */
  if (!argv || !*argv || options & ~FTS_OPTIONMASK ||
      !!(options & FTS_LOGICAL) == !!(options & FTS_PHYSICAL) ||
      (policy == WalkPolicy::Nftw && !nftw_options(options))) {
    errno = EINVAL;
    return (NULL);
  }

  /* Allocate/initialize the stream */
  if ((sp = static_cast<FTS *>(allocate_zero(sizeof(FTS)))) == NULL)
    return (NULL);
  sp->fts_compar = compar;
  sp->fts_options = options;

  /* Logical walks turn on NOCHDIR; symbolic links are too hard. */
  if (ISSET(FTS_LOGICAL))
    SET(FTS_NOCHDIR);

  /*
   * Start out with 1K of path space, and enough, in any case,
   * to hold the user's paths.
   */
  if (fts_palloc(sp, MAX(fts_maxarglen(argv), MAXPATHLEN)))
    goto mem1;

  /* Allocate/initialize root's parent. */
  if ((parent = fts_alloc(sp, "", 0)) == NULL)
    goto mem2;
  parent->fts_level = FTS_ROOTPARENTLEVEL;

  /* Allocate/initialize root(s). */
  for (root = NULL, nitems = 0; *argv; ++argv, ++nitems) {
    /* Don't allow zero-length paths. */
    if ((len = strlen(*argv)) == 0) {
      errno = ENOENT;
      goto mem3;
    }

    if ((p = fts_alloc(sp, *argv, len)) == NULL)
      goto mem3;
    p->fts_level = FTS_ROOTLEVEL;
    p->fts_parent = parent;
    p->fts_accpath = p->fts_name;
    p->fts_info = fts_stat(sp, p, ISSET(FTS_COMFOLLOW), policy);
    if (policy == WalkPolicy::Nftw && p->fts_info == FTS_ERR) {
      // This root has not joined the owned root list yet.
      int error = p->fts_errno;
      fts_free(p);
      errno = error;
      goto mem3;
    }

    /* Command-line "." and ".." are real directories. */
    if (p->fts_info == FTS_DOT)
      p->fts_info = FTS_D;

    /*
     * If comparison routine supplied, traverse in sorted
     * order; otherwise traverse in the order specified.
     */
    if (compar) {
      p->fts_link = root;
      root = p;
    } else {
      p->fts_link = NULL;
      if (root == NULL)
        tmp = root = p;
      else {
        tmp->fts_link = p;
        tmp = p;
      }
    }
  }
  if (compar && nitems > 1) {
    root = fts_sort(sp, root, nitems);
    if (!root)
      goto mem3;
  }

  /*
   * Allocate a dummy pointer and make fts_read think that we've just
   * finished the node before the root(s); set p->fts_info to FTS_INIT
   * so that everything about the "current" node is ignored.
   */
  if ((sp->fts_cur = fts_alloc(sp, "", 0)) == NULL)
    goto mem3;
  sp->fts_cur->fts_link = root;
  sp->fts_cur->fts_info = FTS_INIT;

  /*
   * If using chdir(2), grab a file descriptor pointing to dot to ensure
   * that we can get back here; this could be avoided for some paths,
   * but almost certainly not worth the effort.  Slashes, symbolic links,
   * and ".." are all fairly nasty problems.  Note, if we can't get the
   * descriptor we run anyway, just more slowly.
   */
  if (!ISSET(FTS_NOCHDIR)) {
    if ((sp->fts_rfd = open(".", O_RDONLY | O_CLOEXEC, 0)) == -1)
      SET(FTS_NOCHDIR);
  }

  return (sp);

mem3:
  fts_lfree(root);
  fts_free(parent);
mem2:
  free(sp->fts_path);
mem1:
  free(sp->fts_array);
  free(sp);
  return (NULL);
}

static void fts_load(FTS *sp, FTSENT *p) {
  size_t len;
  char *cp;

  _DIAGASSERT(sp != NULL);
  _DIAGASSERT(p != NULL);

  /*
   * Load the stream structure for the next traversal.  Since we don't
   * actually enter the directory until after the preorder visit, set
   * the fts_accpath field specially so the chdir gets done to the right
   * place and the user can access the first node.  From fts_open it's
   * known that the path will fit.
   */
  len = p->fts_pathlen = p->fts_namelen;
  memmove(sp->fts_path, p->fts_name, len + 1);
  if ((cp = strrchr(p->fts_name, '/')) && (cp != p->fts_name || cp[1])) {
    len = strlen(++cp);
    memmove(p->fts_name, cp, len + 1);
    p->fts_namelen = (len);
  }
  p->fts_accpath = p->fts_path = sp->fts_path;
  sp->fts_dev = p->fts_dev;
}

int walk_close(FTS *sp) {
  FTSENT *freep, *p;
  int saved_errno = 0;

  _DIAGASSERT(sp != NULL);

  /*
   * This still works if we haven't read anything -- the dummy structure
   * points to the root list, so we step through to the end of the root
   * list which has a valid parent pointer.
   */
  if (sp->fts_cur) {
    for (p = sp->fts_cur; p->fts_level >= FTS_ROOTLEVEL;) {
      freep = p;
      p = p->fts_link ? p->fts_link : p->fts_parent;
      if (freep->fts_flags & FTS_SYMFOLLOW)
        (void)close(freep->fts_symfd);
      fts_free(freep);
    }
    fts_free(p);
  }

  /* Free up child linked list, sort array, path buffer. */
  if (sp->fts_child) {
    fts_lfree(sp->fts_child);
    sp->fts_child = NULL;
  }
  if (sp->fts_array)
    free(sp->fts_array);
  free(sp->fts_path);

  /* Return to original directory, save errno if necessary. */
  if (!ISSET(FTS_NOCHDIR)) {
    if (fchdir(sp->fts_rfd) == -1)
      saved_errno = errno;
    (void)close(sp->fts_rfd);
  }

  /* Free up the stream pointer. */
  free(sp);
  if (saved_errno) {
    errno = saved_errno;
    return -1;
  }

  return 0;
}

/*
 * Special case of "/" at the end of the path so that slashes aren't
 * appended which would cause paths to be written as "....//foo".
 */
#define NAPPEND(p)                                                             \
  (p->fts_path[p->fts_pathlen - 1] == '/' ? p->fts_pathlen - 1 : p->fts_pathlen)

static bool stop_on_stat_error(FTS *sp, FTSENT *p, WalkPolicy policy) {
  if (policy != WalkPolicy::Nftw || p->fts_info != FTS_ERR)
    return false;
  // A pending child may have just become current; keep its siblings and
  // ancestors reachable by walk_close before stopping.
  sp->fts_cur = p;
  SET(FTS_STOP);
  errno = p->fts_errno;
  return true;
}

FTSENT *walk_read(FTS *sp, WalkPolicy policy) {
  FTSENT *p, *tmp;
  int instr;
  char *t;
  int saved_errno;

  _DIAGASSERT(sp != NULL);

  if (policy == WalkPolicy::Nftw && !nftw_options(sp->fts_options)) {
    errno = EINVAL;
    return NULL;
  }

  /* If finished or unrecoverable error, return NULL. */
  if (sp->fts_cur == NULL || ISSET(FTS_STOP))
    return (NULL);

  /* Set current node pointer. */
  p = sp->fts_cur;

  /* Save and zero out user instructions. */
  instr = p->fts_instr;
  p->fts_instr = FTS_NOINSTR;

  /* Any type of file may be re-visited; re-stat and re-turn. */
  if (instr == FTS_AGAIN) {
    p->fts_info = fts_stat(sp, p, 0, policy);
    if (stop_on_stat_error(sp, p, policy))
      return NULL;
    return (p);
  }

  /*
   * Following a symlink -- SLNONE test allows application to see
   * SLNONE and recover.  If indirecting through a symlink, have
   * keep a pointer to current location.  If unable to get that
   * pointer, follow fails.
   */
  if (instr == FTS_FOLLOW &&
      (p->fts_info == FTS_SL || p->fts_info == FTS_SLNONE)) {
    p->fts_info = fts_stat(sp, p, 1, policy);
    if (stop_on_stat_error(sp, p, policy))
      return NULL;
    if (p->fts_info == FTS_D && !ISSET(FTS_NOCHDIR)) {
      if ((p->fts_symfd = open(".", O_RDONLY | O_CLOEXEC, 0)) == -1) {
        p->fts_errno = errno;
        p->fts_info = FTS_ERR;
      } else
        p->fts_flags |= FTS_SYMFOLLOW;
    }
    return (p);
  }

  /* Directory in pre-order. */
  if (p->fts_info == FTS_D) {
    /* If skipped or crossed mount point, do post-order visit. */
    if (instr == FTS_SKIP || (ISSET(FTS_XDEV) && p->fts_dev != sp->fts_dev)) {
      if (p->fts_flags & FTS_SYMFOLLOW) {
        (void)close(p->fts_symfd);
        p->fts_flags &= ~FTS_SYMFOLLOW;
      }
      if (sp->fts_child) {
        fts_lfree(sp->fts_child);
        sp->fts_child = NULL;
      }
      p->fts_info = FTS_DP;
      return (p);
    }

    /* Rebuild if only read the names and now traversing. */
    if (sp->fts_child && ISSET(FTS_NAMEONLY)) {
      CLR(FTS_NAMEONLY);
      fts_lfree(sp->fts_child);
      sp->fts_child = NULL;
    }

    /*
     * Cd to the subdirectory.
     *
     * If have already read and now fail to chdir, whack the list
     * to make the names come out right, and set the parent errno
     * so the application will eventually get an error condition.
     * Set the FTS_DONTCHDIR flag so that when we logically change
     * directories back to the parent we don't do a chdir.
     *
     * If haven't read do so.  If the read fails, fts_build sets
     * FTS_STOP or the fts_info field of the node.
     */
    if (sp->fts_child) {
      if (fts_safe_changedir(sp, p, -1, p->fts_accpath)) {
        p->fts_errno = errno;
        p->fts_flags |= FTS_DONTCHDIR;
        for (p = sp->fts_child; p; p = p->fts_link)
          p->fts_accpath = p->fts_parent->fts_accpath;
      }
    } else if ((sp->fts_child = fts_build(sp, BREAD, policy)) == NULL) {
      if (ISSET(FTS_STOP))
        return (NULL);
      return (p);
    }
    p = sp->fts_child;
    sp->fts_child = NULL;
    goto visit;
  }

next:
  /* Move to the next node on this level. */
  tmp = p;

  if ((p = p->fts_link) != NULL) {
    // Keep the remaining tree reachable if restoring cwd fails.
    sp->fts_cur = p;
    fts_free(tmp);

  visit:
    /*
     * If reached the top, return to the original directory, and
     * load the paths for the next root.
     */
    if (p->fts_level == FTS_ROOTLEVEL) {
      if (FCHDIR(sp, sp->fts_rfd)) {
        SET(FTS_STOP);
        return (NULL);
      }
      fts_load(sp, p);
      return (sp->fts_cur = p);
    }

    /*
     * User may have called fts_set on the node.  If skipped,
     * ignore.  If followed, get a file descriptor so we can
     * get back if necessary.
     */
    if (p->fts_instr == FTS_SKIP)
      goto next;
    if (p->fts_instr == FTS_FOLLOW) {
      // Children share the path buffer; select this sibling before stat.
      t = sp->fts_path + NAPPEND(p->fts_parent);
      *t++ = '/';
      memmove(t, p->fts_name, p->fts_namelen + 1);
      p->fts_info = fts_stat(sp, p, 1, policy);
      if (stop_on_stat_error(sp, p, policy))
        return NULL;
      if (p->fts_info == FTS_D && !ISSET(FTS_NOCHDIR)) {
        if ((p->fts_symfd = open(".", O_RDONLY | O_CLOEXEC, 0)) == -1) {
          p->fts_errno = errno;
          p->fts_info = FTS_ERR;
        } else
          p->fts_flags |= FTS_SYMFOLLOW;
      }
      p->fts_instr = FTS_NOINSTR;
    }

    t = sp->fts_path + NAPPEND(p->fts_parent);
    *t++ = '/';
    memmove(t, p->fts_name, (size_t)(p->fts_namelen + 1));
    return (sp->fts_cur = p);
  }

  /* Move up to the parent node. */
  p = tmp->fts_parent;
  sp->fts_cur = p;
  fts_free(tmp);

  if (p->fts_level == FTS_ROOTPARENTLEVEL) {
    /*
     * Done; free everything up and set errno to 0 so the user
     * can distinguish between error and EOF.
     */
    fts_free(p);
    errno = 0;
    return (sp->fts_cur = NULL);
  }

  /* NUL terminate the pathname. */
  sp->fts_path[p->fts_pathlen] = '\0';

  /*
   * Return to the parent directory.  If at a root node or came through
   * a symlink, go back through the file descriptor.  Otherwise, cd up
   * one directory.
   */
  if (p->fts_level == FTS_ROOTLEVEL) {
    if (FCHDIR(sp, sp->fts_rfd)) {
      SET(FTS_STOP);
      return (NULL);
    }
  } else if (p->fts_flags & FTS_SYMFOLLOW) {
    if (FCHDIR(sp, p->fts_symfd)) {
      saved_errno = errno;
      (void)close(p->fts_symfd);
      p->fts_flags &= ~FTS_SYMFOLLOW;
      errno = saved_errno;
      SET(FTS_STOP);
      return (NULL);
    }
    (void)close(p->fts_symfd);
    p->fts_flags &= ~FTS_SYMFOLLOW;
  } else if (!(p->fts_flags & FTS_DONTCHDIR) &&
             fts_safe_changedir(sp, p->fts_parent, -1, "..")) {
    SET(FTS_STOP);
    return (NULL);
  }
  p->fts_info = p->fts_errno ? FTS_ERR : FTS_DP;
  return (sp->fts_cur = p);
}

PrepareResult walk_prepare_directory(FTS *sp, WalkPolicy policy) {
  _DIAGASSERT(sp != NULL);
  FTSENT *cur = sp->fts_cur;
  if (!ISSET(FTS_NOCHDIR) || ISSET(FTS_NOSTAT | FTS_NAMEONLY | FTS_STOP) ||
      (policy == WalkPolicy::Nftw && !nftw_options(sp->fts_options)) || !cur ||
      cur->fts_info != FTS_D || sp->fts_child ||
      (cur->fts_instr != 0 && cur->fts_instr != FTS_NOINSTR) ||
      (ISSET(FTS_XDEV) && cur->fts_dev != sp->fts_dev)) {
    errno = EINVAL;
    return PrepareResult::Error;
  }

  // BREAD transfers a complete list to the stream, or retires the entire
  // partial list on failure. DP/DNR encode terminal preparation outcomes so
  // the next read advances past this entry instead of reopening it.
  size_t pathlen = cur->fts_pathlen;
  char last = sp->fts_path[pathlen - 1];
  errno = 0;
  sp->fts_child = fts_build(sp, BREAD, policy);
  int error = errno;
  // fts_build can both reallocate the path and erase its trailing slash.
  sp->fts_path[pathlen - 1] = last;
  sp->fts_path[pathlen] = '\0';

  if (ISSET(FTS_STOP) || cur->fts_info == FTS_DNR) {
    if (!ISSET(FTS_STOP) && cur->fts_errno == EACCES) {
      errno = EACCES;
      return PrepareResult::Unreadable;
    }
    cur->fts_info = FTS_ERR;
    cur->fts_errno = error;
    SET(FTS_STOP);
    errno = error;
    return PrepareResult::Error;
  }
  errno = 0;
  return sp->fts_child ? PrepareResult::Ready : PrepareResult::Empty;
}

/*
 * Fts_set takes the stream as an argument although it's not used in this
 * implementation; it would be necessary if anyone wanted to add global
 * semantics to fts using fts_set.  An error return is allowed for similar
 * reasons.
 */
/* ARGSUSED */
int walk_set(FTS *sp, FTSENT *p, int instr) {

  _DIAGASSERT(sp != NULL);
  _DIAGASSERT(p != NULL);

  if (instr && instr != FTS_AGAIN && instr != FTS_FOLLOW &&
      instr != FTS_NOINSTR && instr != FTS_SKIP) {
    errno = EINVAL;
    return (1);
  }
  p->fts_instr = instr;
  return (0);
}

FTSENT *walk_children(FTS *sp, int instr) {
  FTSENT *p;
  int fd;

  _DIAGASSERT(sp != NULL);

  if (instr && instr != FTS_NAMEONLY) {
    errno = EINVAL;
    return (NULL);
  }

  /* Set current node pointer. */
  p = sp->fts_cur;

  /*
   * Errno set to 0 so user can distinguish empty directory from
   * an error.
   */
  errno = 0;

  /* Fatal errors stop here. */
  if (ISSET(FTS_STOP) || p == NULL)
    return (NULL);

  /* Return logical hierarchy of user's arguments. */
  if (p->fts_info == FTS_INIT)
    return (p->fts_link);

  /*
   * If not a directory being visited in pre-order, stop here.  Could
   * allow FTS_DNR, assuming the user has fixed the problem, but the
   * same effect is available with FTS_AGAIN.
   */
  if (p->fts_info != FTS_D /* && p->fts_info != FTS_DNR */)
    return (NULL);

  /* Free up any previous child list. */
  if (sp->fts_child) {
    fts_lfree(sp->fts_child);
    sp->fts_child = NULL;
  }

  if (instr == FTS_NAMEONLY) {
    SET(FTS_NAMEONLY);
    instr = BNAMES;
  } else {
    CLR(FTS_NAMEONLY);
    instr = BCHILD;
  }

  /*
   * If using chdir on a relative path and called BEFORE fts_read does
   * its chdir to the root of a traversal, we can lose -- we need to
   * chdir into the subdirectory, and we don't know where the current
   * directory is, so we can't get back so that the upcoming chdir by
   * fts_read will work.
   */
  if (p->fts_level != FTS_ROOTLEVEL || p->fts_accpath[0] == '/' ||
      ISSET(FTS_NOCHDIR))
    return (sp->fts_child = fts_build(sp, instr, WalkPolicy::Fts));

  if ((fd = open(".", O_RDONLY | O_CLOEXEC, 0)) == -1)
    return (sp->fts_child = NULL);
  sp->fts_child = fts_build(sp, instr, WalkPolicy::Fts);
  if (fchdir(fd)) {
    int saved_errno = errno;
    (void)close(fd);
    SET(FTS_STOP);
    errno = saved_errno;
    return (NULL);
  }
  (void)close(fd);
  return (sp->fts_child);
}

/*
 * This is the tricky part -- do not casually change *anything* in here.  The
 * idea is to build the linked list of entries that are used by fts_children
 * and fts_read.  There are lots of special cases.
 *
 * Physical NOSTAT walks use d_type to skip stat for known non-directories.
 * Unknown entries still require stat; directory link counts are not reliable
 * on all Linux filesystems.
 */
static FTSENT *fts_build(FTS *sp, int type, WalkPolicy policy) {
  struct dirent *dp;
  FTSENT *p, *head;
  size_t nitems;
  FTSENT *cur, *tail;
  Directory *dirp;
  char *oldaddr;
  size_t dnamlen;
  int cderrno, descend, level, nlinks, saved_errno, nostat;
  size_t len, maxlen;
  char *cp = NULL; /* pacify gcc */

  _DIAGASSERT(sp != NULL);

  /* Set current node pointer. */
  cur = sp->fts_cur;

  /*
   * Open the directory for reading.  If this fails, we're done.
   * If being called from fts_read, set the fts_info field.
   */
  if ((dirp = opendir(cur->fts_accpath)) == NULL) {
    if (type == BREAD) {
      cur->fts_info = FTS_DNR;
      cur->fts_errno = errno;
      if (policy == WalkPolicy::Nftw && errno != EACCES) {
        cur->fts_info = FTS_ERR;
        SET(FTS_STOP);
      }
    }
    return (NULL);
  }

  /*
   * Zero suppresses all stat calls for a names-only list; -1 requires stat
   * except for the known non-directory optimization below.
   */
  if (type == BNAMES) {
    nlinks = 0;
    nostat = 1;
  } else if (ISSET(FTS_NOSTAT) && ISSET(FTS_PHYSICAL)) {
    // Linux filesystems need not encode subdirectory counts in nlink.
    nlinks = -1;
    nostat = 1;
  } else {
    nlinks = -1;
    nostat = 0;
  }

  /*
   * If we're going to need to stat anything or we want to descend
   * and stay in the directory, chdir.  If this fails we keep going,
   * but set a flag so we don't chdir after the post-order visit.
   * We won't be able to stat anything, but we can still return the
   * names themselves.  Note, that since fts_read won't be able to
   * chdir into the directory, it will have to return different path
   * names than before, i.e. "a/b" instead of "b".  Since the node
   * has already been visited in pre-order, have to wait until the
   * post-order visit to return the error.  There is a special case
   * here, if there was nothing to stat then it's not an error to
   * not be able to stat.  This is all fairly nasty.  If a program
   * needed sorted entries or stat information, they had better be
   * checking FTS_NS on the returned nodes.
   */
  cderrno = 0;
  if (nlinks || type == BREAD) {
    if (fts_safe_changedir(sp, cur, dirfd(dirp), NULL)) {
      if (nlinks && type == BREAD)
        cur->fts_errno = errno;
      cur->fts_flags |= FTS_DONTCHDIR;
      descend = 0;
      cderrno = errno;
    } else
      descend = 1;
  } else
    descend = 0;

  /*
   * Figure out the max file name length that can be stored in the
   * current path -- the inner loop allocates more path as necessary.
   * We really wouldn't have to do the maxlen calculations here, we
   * could do them in fts_read before returning the path, but it's a
   * lot easier here since the length is part of the dirent structure.
   *
   * If not changing directories set a pointer so that can just append
   * each new name into the path.
   */
  len = NAPPEND(cur);
  if (ISSET(FTS_NOCHDIR)) {
    cp = sp->fts_path + len;
    *cp++ = '/';
  }
  len++;
  maxlen = sp->fts_pathlen - len;

  if (cur->fts_level == INT_MAX) {
    closedir(dirp);
    cur->fts_info = FTS_ERR;
    if (policy == WalkPolicy::Nftw)
      cur->fts_errno = EOVERFLOW;
    SET(FTS_STOP);
    errno = EOVERFLOW;
    return NULL;
  }
  level = cur->fts_level + 1;

  /* Read the directory, attaching each entry to the `link' pointer. */
  for (head = tail = NULL, nitems = 0; (dp = readdir(dirp)) != NULL;) {

    if (!ISSET(FTS_SEEDOT) && ISDOT(dp->d_name))
      continue;

    dnamlen = strlen(dp->d_name);
    if ((p = fts_alloc(sp, dp->d_name, dnamlen)) == NULL)
      goto mem1;
    if (dnamlen >= maxlen) { /* include space for NUL */
      oldaddr = sp->fts_path;
      if (fts_palloc(sp, dnamlen + len + 1)) {
        /*
         * No more memory for path or structures.  Save
         * errno, free up the current structure and the
         * structures already allocated.
         */
      mem1:
        saved_errno = errno;
        if (p)
          fts_free(p);
        fts_lfree(head);
        (void)closedir(dirp);
        errno = saved_errno;
        cur->fts_info = FTS_ERR;
        if (policy == WalkPolicy::Nftw)
          cur->fts_errno = saved_errno;
        SET(FTS_STOP);
        return (NULL);
      }
      /* Did realloc() change the pointer? */
      if (oldaddr != sp->fts_path) {
        // Rebase before stat, not after the directory has been read.
        fts_padjust(sp, head ? head : cur);
        p->fts_path = sp->fts_path;
        if (ISSET(FTS_NOCHDIR))
          cp = sp->fts_path + len;
      }
      maxlen = sp->fts_pathlen - len;
    }

    p->fts_level = level;
    p->fts_pathlen = (len + dnamlen);
    p->fts_parent = sp->fts_cur;

    if (cderrno) {
      if (nlinks) {
        p->fts_info = FTS_NS;
        p->fts_errno = cderrno;
      } else
        p->fts_info = FTS_NSOK;
      p->fts_accpath = cur->fts_accpath;
    } else if (nlinks == 0
#ifdef DT_DIR
               || (nostat && dp->d_type != DT_DIR && dp->d_type != DT_UNKNOWN)
#endif
    ) {
      p->fts_accpath = ISSET(FTS_NOCHDIR) ? p->fts_path : p->fts_name;
      p->fts_info = FTS_NSOK;
    } else {
      /* Build a file name for fts_stat to stat. */
      if (ISSET(FTS_NOCHDIR)) {
        p->fts_accpath = p->fts_path;
        memmove(cp, p->fts_name, (size_t)(p->fts_namelen + 1));
      } else
        p->fts_accpath = p->fts_name;
      /* Stat it. */
      p->fts_info = fts_stat(sp, p, 0, policy);
      if (policy == WalkPolicy::Nftw && p->fts_info == FTS_ERR) {
        // Do not queue a fatal stat error behind callbacks for other entries.
        // p is not linked yet; mem1 owns it and the accumulated sibling list.
        errno = p->fts_errno;
        goto mem1;
      }
    }

    /* We walk in directory order so "ls -f" doesn't get upset. */
    p->fts_link = NULL;
    if (head == NULL)
      head = tail = p;
    else {
      tail->fts_link = p;
      tail = p;
    }
    ++nitems;
  }
  saved_errno = errno;
  (void)closedir(dirp);
  if (saved_errno) {
    fts_lfree(head);
    cur->fts_info = FTS_ERR;
    cur->fts_errno = saved_errno;
    SET(FTS_STOP);
    errno = saved_errno;
    return NULL;
  }

  /*
   * If not changing directories, reset the path back to original
   * state.
   */
  if (ISSET(FTS_NOCHDIR)) {
    if (len == sp->fts_pathlen || nitems == 0)
      --cp;
    *cp = '\0';
  }

  /*
   * If descended after called from fts_children or after called from
   * fts_read and nothing found, get back.  At the root level we use
   * the saved fd; if one of walk_open()'s arguments is a relative path
   * to an empty directory, we wind up here with no other way back.  If
   * can't get back, we're done.
   */
  if (descend && (type == BCHILD || !nitems) &&
      (cur->fts_level == FTS_ROOTLEVEL
           ? FCHDIR(sp, sp->fts_rfd)
           : fts_safe_changedir(sp, cur->fts_parent, -1, ".."))) {
    cur->fts_info = FTS_ERR;
    SET(FTS_STOP);
    fts_lfree(head);
    return (NULL);
  }

  /* If didn't find anything, return NULL. */
  if (!nitems) {
    if (type == BREAD)
      cur->fts_info = FTS_DP;
    return (NULL);
  }

  /* Sort the entries. */
  if (sp->fts_compar && nitems > 1)
    head = fts_sort(sp, head, nitems);
  return (head);
}

static unsigned short fts_stat(FTS *sp, FTSENT *p, int follow,
                              WalkPolicy policy) {
  FTSENT *t;
  dev_t dev;
  __fts_ino_t ino;
  __fts_stat_t *sbp, sb;
  int saved_errno;

  _DIAGASSERT(sp != NULL);
  _DIAGASSERT(p != NULL);

  /* If user needs stat info, stat buffer already allocated. */
  p->fts_errno = 0;
  p->fts_cycle = NULL;
  sbp = ISSET(FTS_NOSTAT) ? &sb : p->fts_statp;

  /*
   * If doing a logical walk, or application requested FTS_FOLLOW, do
   * a stat(2).  If that fails, check for a non-existent symlink.  If
   * fail, set the errno from the stat call.
   */
  if (ISSET(FTS_LOGICAL) || follow) {
    if (fts::stat(p->fts_accpath, sbp)) {
      saved_errno = errno;
      if ((policy == WalkPolicy::Fts || saved_errno == ENOENT) &&
          !fts::lstat(p->fts_accpath, sbp) && S_ISLNK(sbp->st_mode)) {
        errno = 0;
        return (FTS_SLNONE);
      }
      p->fts_errno = saved_errno;
      goto err;
    }
  } else if (fts::lstat(p->fts_accpath, sbp)) {
    p->fts_errno = errno;
  err:
    memset(sbp, 0, sizeof(*sbp));
    if (policy == WalkPolicy::Nftw) {
      errno = p->fts_errno;
      if (p->fts_errno != EACCES)
        return FTS_ERR;
    }
    return (FTS_NS);
  }

  if (S_ISDIR(sbp->st_mode)) {
    /*
     * Set the device/inode.  Used to find cycles and check for
     * crossing mount points.  Also expose the directory link count.  It is
     * understood that these fields are only referenced if fts_info
     * is set to FTS_D.
     */
    dev = p->fts_dev = sbp->st_dev;
    ino = p->fts_ino = sbp->st_ino;
    p->fts_nlink = sbp->st_nlink;

    if (ISDOT(p->fts_name))
      return (FTS_DOT);

    /*
     * Cycle detection is done by brute force when the directory
     * is first encountered.  If the tree gets deep enough or the
     * number of symbolic links to directories is high enough,
     * something faster might be worthwhile.
     */
    for (t = p->fts_parent; t->fts_level >= FTS_ROOTLEVEL; t = t->fts_parent)
      if (ino == t->fts_ino && dev == t->fts_dev) {
        p->fts_cycle = t;
        return (FTS_DC);
      }
    return (FTS_D);
  }
  if (S_ISLNK(sbp->st_mode))
    return (FTS_SL);
  if (S_ISREG(sbp->st_mode))
    return (FTS_F);
  return (FTS_DEFAULT);
}

static FTSENT *fts_sort(FTS *sp, FTSENT *head, size_t nitems) {
  FTSENT **ap, *p;

  _DIAGASSERT(sp != NULL);
  _DIAGASSERT(head != NULL);

  /*
   * Sort pointers with libc's shared sorting engine. Allocation failure stops
   * the traversal rather than silently breaking the requested order. Reserve
   * 40 extra entries to avoid growing the array one entry at a time.
   */
  if (nitems > sp->fts_nitems) {
    errno = nitems > SIZE_MAX - 40
                ? ENOMEM
                : reallocate_array(&sp->fts_array, nitems + 40,
                                   sizeof(*sp->fts_array));
    if (errno) {
      fts_lfree(head);
      SET(FTS_STOP);
      return NULL;
    }
    sp->fts_nitems = (nitems + 40);
  }
  for (ap = sp->fts_array, p = head; p; p = p->fts_link)
    *ap++ = p;
  internal::unstable_sort(
      sp->fts_array, nitems, sizeof(FTSENT *),
      [](const void *a, const void *b, void *context) {
        const FTSENT *left = *static_cast<FTSENT *const *>(a);
        const FTSENT *right = *static_cast<FTSENT *const *>(b);
        return static_cast<FTS *>(context)->fts_compar(&left, &right);
      },
      sp);
  for (head = *(ap = sp->fts_array); --nitems; ++ap)
    ap[0]->fts_link = ap[1];
  ap[0]->fts_link = NULL;
  return (head);
}

static FTSENT *fts_alloc(FTS *sp, const char *name, size_t namelen) {
  FTSENT *p;

  _DIAGASSERT(sp != NULL);
  _DIAGASSERT(name != NULL);

  if (namelen > SIZE_MAX - sizeof(FTSENT)) {
    errno = ENAMETOOLONG;
    return NULL;
  }
  if ((p = static_cast<FTSENT *>(allocate_zero(sizeof(FTSENT) + namelen))) ==
      NULL)
    return (NULL);

  if (!ISSET(FTS_NOSTAT))
    if ((p->fts_statp = static_cast<struct stat *>(
             ::malloc(sizeof(*(p->fts_statp))))) == NULL) {
      free(p);
      errno = ENOMEM;
      return (NULL);
    }

  if (ISSET(FTS_NOSTAT))
    p->fts_statp = NULL;

  /* Copy the name plus the trailing NULL. */
  memmove(p->fts_name, name, namelen + 1);

  p->fts_namelen = (namelen);
  p->fts_path = sp->fts_path;
  p->fts_errno = 0;
  p->fts_flags = 0;
  p->fts_instr = FTS_NOINSTR;
  p->fts_number = 0;
  p->fts_pointer = NULL;
  return (p);
}

static void fts_free(FTSENT *p) {
  if (p->fts_statp)
    free(p->fts_statp);
  free(p);
}

static void fts_lfree(FTSENT *head) {
  FTSENT *p;

  /* Free a linked list of structures. */
  while ((p = head) != NULL) {
    head = head->fts_link;
    fts_free(p);
  }
}

static size_t fts_pow2(size_t x) {

  x--;
  for (size_t shift = 1; shift < sizeof(x) * CHAR_BIT; shift *= 2)
    x |= x >> shift;
  x++;
  return (x);
}

/*
 * Allow essentially unlimited paths; find, rm, ls should all work on any tree.
 * Most systems will allow creation of paths much longer than MAXPATHLEN, even
 * though the kernel won't resolve them.  Round up the new size to a power
 * of 2, so we don't realloc the path 2 bytes at a time.
 */
static int fts_palloc(FTS *sp, size_t size) {
  char *newpath;

  _DIAGASSERT(sp != NULL);

  if (size > SIZE_MAX / 2 + 1) {
    errno = ENAMETOOLONG;
    return 1;
  }
  size = fts_pow2(size);
  newpath = static_cast<char *>(::realloc(sp->fts_path, size));
  if (newpath == 0) {
    errno = ENOMEM;
    return (1);
  }
  sp->fts_path = newpath;
  sp->fts_pathlen = (size);
  return (0);
}

/*
 * When the path is realloc'd, have to fix all of the pointers in structures
 * already returned.
 */
static void fts_padjust(FTS *sp, FTSENT *head) {
  FTSENT *p;
  char *addr;

  _DIAGASSERT(sp != NULL);

#define ADJUST(p)                                                              \
  do {                                                                         \
    if ((p)->fts_accpath != (p)->fts_name)                                     \
      (p)->fts_accpath =                                                       \
          addr + (reinterpret_cast<uintptr_t>((p)->fts_accpath) -              \
                  reinterpret_cast<uintptr_t>((p)->fts_path));                 \
    (p)->fts_path = addr;                                                      \
  } while (0)

  addr = sp->fts_path;

  /* Adjust the current set of children. */
  for (p = sp->fts_child; p; p = p->fts_link)
    ADJUST(p);

  /* Adjust the rest of the tree, including the current level. */
  for (p = head; p->fts_level >= FTS_ROOTLEVEL;) {
    ADJUST(p);
    p = p->fts_link ? p->fts_link : p->fts_parent;
  }
}

static size_t fts_maxarglen(char *const *argv) {
  size_t len, max;

  _DIAGASSERT(argv != NULL);

  for (max = 0; *argv; ++argv)
    if ((len = strlen(*argv)) > max)
      max = len;
  return (max + 1);
}

/*
 * Change to dir specified by fd or p->fts_accpath without getting
 * tricked by someone changing the world out from underneath us.
 * Assumes p->fts_dev and p->fts_ino are filled in.
 */
static int fts_safe_changedir(const FTS *sp, const FTSENT *p, int fd,
                              const char *path) {
  int oldfd = fd, ret = -1;
  __fts_stat_t sb;

  if (ISSET(FTS_NOCHDIR))
    return 0;

  if (oldfd < 0 && (fd = open(path, O_RDONLY | O_CLOEXEC)) == -1)
    return -1;

  if (fts::fstat(fd, &sb) == -1)
    goto bail;

  if (sb.st_ino != p->fts_ino || sb.st_dev != p->fts_dev) {
    errno = ENOENT;
    goto bail;
  }

  ret = fchdir(fd);

bail:
  if (oldfd < 0) {
    int save_errno = errno;
    (void)close(fd);
    errno = save_errno;
  }
  return ret;
}

#undef errno
} // namespace fts
} // namespace LIBC_NAMESPACE_DECL
