//===-- Tests for execvp path search and shell fallback -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/stdlib/free.h"
#include "src/stdlib/getenv.h"
#include "src/stdlib/malloc.h"
#include "src/unistd/environ.h"
#include "src/unistd/execve.h"
#include "src/unistd/execvp.h"
#include "test/UnitTest/Test.h"

alignas(void *) static char storage[2][1024];
static bool used[2];

static const char *path;
static const char *expected[16];
static int errors[16], calls, expected_calls, allocations, fail_at, live;
static bool mismatch;
static char argument0[] = "custom-argv-zero", argument1[] = "argument",
            argument2[] = "two";
static char *arguments[] = {argument0, argument1, argument2, nullptr};
static char *empty_arguments[] = {nullptr};
static char environment[] = "MARKER=preserved";
static char *environment_vector[] = {environment, nullptr};
static char *const *original = arguments;
static const char *script_path;
static bool equal(const char *a, const char *b) {
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

namespace LIBC_NAMESPACE_DECL {
char **environ = environment_vector;
char *getenv(const char *name) {
  if (!equal(name, "PATH"))
    mismatch = true;
  return const_cast<char *>(path);
}
void *malloc(size_t size) {
  ++allocations;
  if (allocations == fail_at)
    return nullptr;
  for (int i = 0; i < 2; ++i) {
    if (!used[i] && size <= sizeof(storage[i])) {
      used[i] = true;
      ++live;
      return storage[i];
    }
  }
  return nullptr;
}
void free(void *p) {
  if (p) {
    bool found = false;
    for (int i = 0; i < 2; ++i) {
      if (p == storage[i] && used[i]) {
        used[i] = false;
        --live;
        found = true;
        break;
      }
    }
    if (!found)
      mismatch = true;
  }
  libc_errno = EIO; // Cleanup must not overwrite the failed exec's errno.
}
int execve(const char *file, char *const argv[], char *const envp[]) {
  if (calls >= expected_calls || !equal(file, expected[calls]) ||
      envp != environ) {
    mismatch = true;
    libc_errno = EIO;
    return -1;
  }
  if (equal(file, "/bin/sh") && script_path) {
    if (!argv || !argv[0] || !equal(argv[0], "/bin/sh") || !argv[1] ||
        !equal(argv[1], script_path))
      mismatch = true;
    if (original == empty_arguments) {
      if (argv[2])
        mismatch = true;
    } else if (argv[2] != argument1 || argv[3] != argument2 || argv[4])
      mismatch = true;
  } else if (argv != original)
    mismatch = true;
  libc_errno = errors[calls++];
  return -1;
}
} // namespace LIBC_NAMESPACE_DECL

static void reset(const char *value) {
  path = value;
  calls = expected_calls = allocations = fail_at = 0;
  mismatch = false;
  script_path = nullptr;
  original = arguments;
  libc_errno = 0;
}
static void expect(const char *name, int error) {
  expected[expected_calls] = name;
  errors[expected_calls++] = error;
}
#define CHECK(c) ASSERT_TRUE(c)
#define EXEC(name, error)                                                      \
  do {                                                                         \
    CHECK(LIBC_NAMESPACE::execvp(name, original) == -1);                       \
    CHECK(int(libc_errno) == error && calls == expected_calls && !mismatch &&  \
          live == 0);                                                          \
  } while (0)
TEST(LlvmLibcExecvpTest, SearchAndFallback) {
  reset("/ignored");
  EXEC("", ENOENT);
  CHECK(allocations == 0);
  reset("/ignored");
  expect("relative/tool", EPERM);
  EXEC("relative/tool", EPERM);
  CHECK(allocations == 0);
  reset("/ignored");
  expect("/absolute/tool", ENOENT);
  EXEC("/absolute/tool", ENOENT);
  reset(nullptr);
  expect("/usr/bin/tool", ENOENT);
  expect("/bin/tool", ENOENT);
  EXEC("tool", ENOENT);
  reset("");
  expect("tool", ENOENT);
  EXEC("tool", ENOENT);
  reset(":a::b/:");
  expect("tool", ENOENT);
  expect("a/tool", ENOTDIR);
  expect("tool", EACCES);
  expect("b/tool", ENOENT);
  expect("tool", ENOENT);
  EXEC("tool", EACCES);
  reset("/a:/b");
  expect("/a/tool", ENOTDIR);
  expect("/b/tool", ENOENT);
  EXEC("tool", ENOENT);
  reset("/a:/b");
  expect("/a/tool", EACCES);
  expect("/b/tool", EIO);
  EXEC("tool", EIO);
  const int terminal_errors[] = {E2BIG, ENOMEM, ETXTBSY, ELOOP, ENAMETOOLONG};
  for (int error : terminal_errors) {
    reset("/a:/b");
    expect("/a/tool", error);
    EXEC("tool", error);
  }
  reset("/ignored");
  script_path = "./script";
  expect("./script", ENOEXEC);
  expect("/bin/sh", EPERM);
  EXEC("./script", EPERM);
  reset("/a:/b");
  script_path = "/b/script";
  expect("/a/script", ENOENT);
  expect("/b/script", ENOEXEC);
  expect("/bin/sh", ENOENT);
  EXEC("script", ENOENT);
  reset("/a:/b");
  script_path = "/a/script";
  expect("/a/script", ENOEXEC);
  expect("/bin/sh", ENOEXEC);
  EXEC("script", ENOEXEC);
  reset("");
  script_path = "script";
  original = empty_arguments;
  expect("script", ENOEXEC);
  expect("/bin/sh", EACCES);
  EXEC("script", EACCES);
  reset("/a:/b");
  fail_at = 1;
  EXEC("tool", ENOMEM);
  reset("/a:/b");
  fail_at = 2;
  expect("/a/tool", EACCES);
  EXEC("tool", ENOMEM);
  reset("/a:/b");
  fail_at = 2;
  expect("/a/script", ENOEXEC);
  EXEC("script", ENOMEM);
  reset(nullptr);
  fail_at = 1;
  expect("./script", ENOEXEC);
  EXEC("./script", ENOMEM);
}
