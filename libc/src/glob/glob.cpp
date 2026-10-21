//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/glob/glob.h"
#include "hdr/errno_macros.h"
#include "hdr/fnmatch_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/realloc.h"
#include "hdr/glob_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/File/dir.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/fnmatch/fnmatch.h"
#include "src/stdlib/qsort_util.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "src/string/strcoll.h"
#include "src/string/string_utils.h"
#include "src/sys/stat/lstat.h"
#include "src/sys/stat/stat.h"

namespace LIBC_NAMESPACE_DECL {
namespace {

constexpr size_t MAX_SIZE = cpp::numeric_limits<size_t>::max();

struct Buffer {
  char *data = nullptr;
  size_t capacity = 0;
  Buffer() = default;
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
  ~Buffer() { ::free(data); }

  bool join(const char *prefix, const char *suffix, size_t length) {
    size_t before = internal::string_length(prefix);
    if (length >= MAX_SIZE - before)
      return false;
    size_t needed = before + length + 1;
    if (needed > capacity) {
      size_t next_capacity = capacity > MAX_SIZE / 2 ? MAX_SIZE : capacity * 2;
      if (next_capacity < needed)
        next_capacity = needed;
      bool prefix_is_buffer = prefix == data;
      auto *next = static_cast<char *>(::realloc(data, next_capacity));
      if (!next)
        return false;
      if (prefix_is_buffer)
        prefix = next;
      data = next;
      capacity = next_capacity;
    }
    if (prefix != data)
      inline_memcpy(data, prefix, before);
    inline_memcpy(data + before, suffix, length);
    data[before + length] = '\0';
    return true;
  }
};

struct Directory {
  Dir *dir;
  ~Directory() {
    int saved = libc_errno;
    dir->close(/*consume_on_error=*/true);
    libc_errno = saved;
  }
};

bool has_magic(const char *pattern, bool noescape) {
  for (const char *p = pattern; *p; ++p) {
    if (*p == '\\' && !noescape && p[1])
      ++p;
    else if (*p == '*' || *p == '?')
      return true;
    else if (*p == '[') {
      const char *end = p + 1;
      if (*end == '!' || *end == '^')
        ++end;
      if (*end == ']')
        ++end;
      for (; *end; ++end) {
        if (*end == '\\' && !noescape && end[1])
          ++end;
        else if (*end == ']')
          return true;
      }
    }
  }
  return false;
}

class Expansion {
  glob_t &result;
  int flags;
  int (*errfunc)(const char *, int);
  size_t capacity;

  int directory_error(const char *path, int error) {
    libc_errno = error;
    if (error == ENOMEM)
      return GLOB_NOSPACE;
    if ((errfunc && errfunc(path, error)) || (flags & GLOB_ERR))
      return GLOB_ABORTED;
    return 0;
  }

  int path_error(const char *path) {
    int error = libc_errno;
    if (error == ENOENT || error == ENOTDIR)
      return 0;
    return directory_error(path, error);
  }

public:
  Expansion(glob_t &result, int flags, int (*errfunc)(const char *, int))
      : result(result), flags(flags), errfunc(errfunc),
        capacity(result.gl_pathc) {}

  int append(const char *path, bool mark = false) {
    if (result.gl_pathc == capacity) {
      size_t limit = MAX_SIZE / sizeof(char *);
      if (result.gl_offs >= limit ||
          result.gl_pathc >= limit - result.gl_offs - 1)
        return GLOB_NOSPACE;
      size_t available = limit - result.gl_offs - 1;
      size_t next = capacity > available / 2 ? available : capacity * 2;
      if (next < 8)
        next = available < 8 ? available : 8;
      auto *storage = static_cast<char **>(::realloc(
          result.gl_pathv, (result.gl_offs + next + 1) * sizeof(char *)));
      if (!storage)
        return GLOB_NOSPACE;
      if (!result.gl_pathv)
        for (size_t i = 0; i < result.gl_offs; ++i)
          storage[i] = nullptr;
      result.gl_pathv = storage;
      capacity = next;
      storage[result.gl_offs + result.gl_pathc] = nullptr;
    }
    Buffer copy;
    if (!copy.join(path, mark ? "/" : "", mark ? 1 : 0))
      return GLOB_NOSPACE;
    result.gl_pathv[result.gl_offs + result.gl_pathc++] = copy.data;
    copy.data = nullptr;
    result.gl_pathv[result.gl_offs + result.gl_pathc] = nullptr;
    return 0;
  }

  int expand(const char *pattern, const char *prefix) {
    Buffer base;
    // Literal components do not need recursive frames or directory reads.
    for (;;) {
      // Preserve separators, including an absolute path's leading slash.
      const char *part = pattern;
      while (*part == '/')
        ++part;
      if (!base.join(prefix, pattern, part - pattern))
        return GLOB_NOSPACE;
      if (!*part) {
        struct stat status;
        if (!*base.data)
          return 0;
        if (LIBC_NAMESPACE::lstat(base.data, &status) < 0)
          return path_error(base.data);
        bool mark = false;
        if (flags & GLOB_MARK)
          mark = LIBC_NAMESPACE::stat(base.data, &status) == 0 &&
                 S_ISDIR(status.st_mode) &&
                 base.data[internal::string_length(base.data) - 1] != '/';
        return append(base.data, mark);
      }

      bool noescape = flags & GLOB_NOESCAPE;
      const char *end = part;
      while (*end && *end != '/') {
        if (!noescape && *end == '\\' && end[1]) {
          if (end[1] == '/')
            break;
          ++end;
        }
        ++end;
      }
      Buffer component;
      if (!component.join("", part, end - part))
        return GLOB_NOSPACE;
      if (!noescape && *end == '\\' && end[1] == '/')
        ++end;
      if (!has_magic(component.data, noescape)) {
        char *out = component.data;
        for (const char *p = component.data; *p; ++p) {
          if (*p == '\\' && !noescape && p[1])
            ++p;
          *out++ = *p;
        }
        *out = '\0';
        if (!base.join(base.data, component.data, out - component.data))
          return GLOB_NOSPACE;
        prefix = base.data;
        pattern = end;
        continue;
      }

      const char *directory = *base.data ? base.data : ".";
      auto opened = Dir::open(directory);
      if (!opened)
        return directory_error(directory, opened.error());
      Directory owner{opened.value()};
      for (;;) {
        auto entry = owner.dir->read();
        if (!entry)
          return directory_error(directory, entry.error());
        if (!entry.value())
          return 0;
        const char *name = entry.value()->d_name;
        int match_flags = FNM_PERIOD | (noescape ? FNM_NOESCAPE : 0);
        if (LIBC_NAMESPACE::fnmatch(component.data, name, match_flags))
          continue;
        Buffer next;
        if (!next.join(base.data, name, internal::string_length(name)))
          return GLOB_NOSPACE;
        // Non-directories are not errors when expanding an intermediate '*'.
        if (*end == '/') {
          struct stat status;
          if (LIBC_NAMESPACE::stat(next.data, &status) < 0) {
            int error = path_error(next.data);
            if (error)
              return error;
            continue;
          }
          if (!S_ISDIR(status.st_mode))
            continue;
        }
        int error = expand(end, next.data);
        if (error)
          return error;
      }
    }
  }
};

int compare_paths(const void *left, const void *right) {
  return LIBC_NAMESPACE::strcoll(*static_cast<char *const *>(left),
                                 *static_cast<char *const *>(right));
}

// Roll back only this call's additions if a callback throws or unwinds.
struct ResultTransaction {
  glob_t &result;
  size_t previous;
  bool complete = false;
  ~ResultTransaction() {
    if (complete)
      return;
    while (result.gl_pathc > previous)
      ::free(result.gl_pathv[result.gl_offs + --result.gl_pathc]);
    if (!previous) {
      ::free(result.gl_pathv);
      result.gl_pathv = nullptr;
    } else {
      result.gl_pathv[result.gl_offs + previous] = nullptr;
    }
  }
};

} // namespace

LLVM_LIBC_FUNCTION(int, glob,
                   (const char *__restrict pattern, int flags,
                    int (*errfunc)(const char *, int),
                    glob_t *__restrict result)) {
  if (!(flags & GLOB_APPEND)) {
    result->gl_pathc = 0;
    result->gl_pathv = nullptr;
    if (!(flags & GLOB_DOOFFS))
      result->gl_offs = 0;
  }
  size_t previous = result->gl_pathc;
  ResultTransaction transaction{*result, previous};
  Expansion expansion(*result, flags, errfunc);
  int error = expansion.expand(pattern, "");
  if (!error && result->gl_pathc == previous) {
    if (flags & GLOB_NOCHECK)
      error = expansion.append(pattern);
    else
      error = GLOB_NOMATCH;
  }
  // An aborted search still exposes its partial results for globfree.
  if (!(flags & GLOB_NOSORT) && result->gl_pathc > previous)
    internal::unstable_sort(result->gl_pathv + result->gl_offs + previous,
                            result->gl_pathc - previous, sizeof(char *),
                            compare_paths);
  transaction.complete = true;
  return error;
}

} // namespace LIBC_NAMESPACE_DECL
