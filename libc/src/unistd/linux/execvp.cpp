//===-- Implementation of execvp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/execvp.h"
#include "src/__support/CPP/stringstream.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/stdlib/free.h"
#include "src/stdlib/getenv.h"
#include "src/stdlib/malloc.h"
#include "src/unistd/environ.h"
#include "src/unistd/execve.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
constexpr char DEFAULT_PATH[] = "/usr/bin:/bin";
constexpr char SHELL_PATH[] = "/bin/sh";

int execute_script(const char *file, char *const argv[]) {
  size_t argc = 0;
  constexpr size_t MAX_ARGS = size_t(-1) / sizeof(char *) - 2;
  while (argv && argv[argc]) {
    if (argc == MAX_ARGS) {
      libc_errno = E2BIG;
      return -1;
    }
    ++argc;
  }
  size_t entries = argc ? argc + 2 : 3;
  auto **args =
      static_cast<char **>(LIBC_NAMESPACE::malloc(entries * sizeof(char *)));
  if (!args) {
    libc_errno = ENOMEM;
    return -1;
  }
  args[0] = const_cast<char *>(SHELL_PATH);
  args[1] = const_cast<char *>(file);
  for (size_t i = 1; i < argc; ++i)
    args[i + 1] = argv[i];
  args[entries - 1] = nullptr;
  int result =
      LIBC_NAMESPACE::execve(SHELL_PATH, args, LIBC_NAMESPACE::environ);
  int error = libc_errno;
  LIBC_NAMESPACE::free(args);
  libc_errno = error;
  return result;
}
} // namespace

LLVM_LIBC_FUNCTION(int, execvp, (const char *file, char *const argv[])) {
  cpp::string_view name(file);
  if (name.empty()) {
    libc_errno = ENOENT;
    return -1;
  }
  if (name.contains('/')) {
    int result = LIBC_NAMESPACE::execve(file, argv, LIBC_NAMESPACE::environ);
    return result == -1 && libc_errno == ENOEXEC ? execute_script(file, argv)
                                                 : result;
  }

  const char *value = LIBC_NAMESPACE::getenv("PATH");
  cpp::string_view path(value ? value : DEFAULT_PATH);
  bool denied = false;
  for (;;) {
    size_t separator = path.find_first_of(':');
    cpp::string_view directory = path.substr(0, separator);
    size_t size;
    if (__builtin_add_overflow(directory.size(), name.size(), &size) ||
        __builtin_add_overflow(size, size_t(2), &size)) {
      libc_errno = ENAMETOOLONG;
      return -1;
    }
    char *candidate = static_cast<char *>(LIBC_NAMESPACE::malloc(size));
    if (!candidate) {
      libc_errno = ENOMEM;
      return -1;
    }
    cpp::StringStream stream({candidate, size});
    if (!directory.empty()) {
      stream << directory;
      if (directory[directory.size() - 1] != '/')
        stream << '/';
    }
    stream << name << '\0';
    int result =
        LIBC_NAMESPACE::execve(candidate, argv, LIBC_NAMESPACE::environ);
    bool script = result == -1 && libc_errno == ENOEXEC;
    if (script)
      result = execute_script(candidate, argv);
    int error = libc_errno;
    LIBC_NAMESPACE::free(candidate);
    libc_errno = error;
    // Once a script is selected, report the shell's failure rather than trying
    // another PATH entry, even if the shell itself was not found.
    if (result != -1 || script)
      return result;
    if (error == EACCES)
      denied = true;
    else if (error != ENOENT && error != ENOTDIR)
      return -1;
    if (separator == cpp::string_view::npos)
      break;
    path.remove_prefix(separator + 1);
  }
  libc_errno = denied ? EACCES : ENOENT;
  return -1;
}
} // namespace LIBC_NAMESPACE_DECL
