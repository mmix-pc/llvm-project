//===-- Linux implementation of posix_spawnp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/spawn/posix_spawnp.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "src/__support/CPP/stringstream.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/spawn/linux/spawn.h"
#include "src/stdlib/getenv.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, posix_spawnp,
                   (pid_t *__restrict pid, const char *__restrict file,
                    const posix_spawn_file_actions_t *actions,
                    const posix_spawnattr_t *__restrict attr,
                    char *const *__restrict argv,
                    char *const *__restrict envp)) {
  cpp::string_view name(file);
  if (name.empty())
    return ENOENT;
  if (name.contains('/')) {
    const char *paths[] = {file, nullptr};
    return spawn(pid, paths, actions, attr, argv, envp);
  }

  // Search the caller's PATH, not envp. Copy all candidates before cloning;
  // getenv, allocation and formatting must never run in the spawn child.
  const char *value = LIBC_NAMESPACE::getenv("PATH");
  cpp::string_view path(value ? value : "/usr/bin:/bin");
  size_t count = 1;
  for (char c : path)
    if (c == ':')
      ++count;
  size_t per_path, strings_size, pointers_size, total;
  if (__builtin_add_overflow(name.size(), size_t(2), &per_path) ||
      __builtin_mul_overflow(count, per_path, &strings_size) ||
      __builtin_add_overflow(strings_size, path.size(), &strings_size) ||
      __builtin_add_overflow(count, size_t(1), &pointers_size) ||
      __builtin_mul_overflow(pointers_size, sizeof(char *), &pointers_size) ||
      __builtin_add_overflow(pointers_size, strings_size, &total))
    return ENAMETOOLONG;

  int saved_errno = libc_errno;
  auto **paths = static_cast<const char **>(::malloc(total));
  if (!paths) {
    libc_errno = saved_errno;
    return ENOMEM;
  }
  char *next = reinterpret_cast<char *>(paths) + pointers_size;
  for (size_t i = 0; i < count; ++i) {
    size_t separator = path.find_first_of(':');
    cpp::string_view directory = path.substr(0, separator);
    // Reserve a slash even for an empty entry (which means the current
    // directory). The extra byte need not be used by the formatted path.
    size_t capacity = directory.size() + per_path;
    paths[i] = next;
    cpp::StringStream stream({next, capacity});
    if (!directory.empty())
      stream << directory << '/';
    stream << name << '\0';
    next += capacity;
    if (separator != cpp::string_view::npos)
      path.remove_prefix(separator + 1);
  }
  paths[count] = nullptr;
  int result = spawn(pid, paths, actions, attr, argv, envp);
  ::free(paths);
  libc_errno = saved_errno;
  return result;
}

} // namespace LIBC_NAMESPACE_DECL
