//===-- Real word expansion service tests ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/app.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_wait_macros.h"
#include "hdr/wordexp_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/locale/locale.h"
#include "src/pwd/getpwnam_r.h"
#include "src/stdlib/environ_internal.h"
#include "src/wordexp/runtime.h"
#include "src/wordexp/wordexp.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
int account_error = ENOENT;
}

namespace LIBC_NAMESPACE_DECL {
AppProperties app;
// The native provider does not select the account entrypoint. These tests use
// HOME, not a named account; fail any unexpected lookup rather than claim
// account-database coverage.
int getpwnam_r(const char *, struct passwd *, char *, size_t,
               struct passwd **) {
  return account_error;
}
} // namespace LIBC_NAMESPACE_DECL

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::syscall_impl;
namespace {
struct Result {
  wordexp_t value{};
  ~Result() { release_word_results(value); }
  WordEvaluationResult run(const char *text, int flags = 0) {
    return evaluate_wordexp_runtime(
        text, value, flags, RestartableConverter(false), nullptr, nullptr);
  }
};
int descriptors() {
  int count = 0;
  for (int fd = 0; fd < 256; ++fd)
    count += syscall_impl<long>(SYS_fcntl, fd, F_GETFD, 0) >= 0;
  return count;
}
} // namespace

TEST(LlvmLibcWordRuntimeTest, RealEnvironmentAssignmentAndCommandStatus) {
  auto &environment = EnvironmentManager::get_instance();
  environment.unset("IFS");
  environment.unset("LLVM_LIBC_WORD_ASSIGN");
  ASSERT_EQ(environment.set("HOME", "/home/space value", true), 0);
  Result result;
  int before = descriptors();
  ASSERT_EQ(
      result
          .run("${LLVM_LIBC_WORD_ASSIGN:=value} "
               "\"$(printf '%s\\n\\n' \"$LLVM_LIBC_WORD_ASSIGN\"; exit 7)\" "
               "~ $((LLVM_LIBC_WORD_NUMBER=2+3))")
          .status,
      0);
  ASSERT_EQ(result.value.we_wordc, size_t(4));
  EXPECT_STREQ(result.value.we_wordv[0], "value");
  EXPECT_STREQ(result.value.we_wordv[1], "value");
  EXPECT_STREQ(result.value.we_wordv[2], "/home/space value");
  EXPECT_STREQ(result.value.we_wordv[3], "5");
  EXPECT_STREQ(environment.get("LLVM_LIBC_WORD_ASSIGN"), "value");
  EXPECT_STREQ(environment.get("LLVM_LIBC_WORD_NUMBER"), "5");
  EXPECT_EQ(descriptors(), before);
  EXPECT_EQ(syscall_impl<long>(SYS_wait4, -1, nullptr, WNOHANG, nullptr),
            -long(ECHILD));
  environment.unset("LLVM_LIBC_WORD_ASSIGN");
  environment.unset("LLVM_LIBC_WORD_NUMBER");
  environment.unset("HOME");
}

TEST(LlvmLibcWordRuntimeTest, DollarDocumentDelimiterReachesActualShell) {
  Result result;
  int before = descriptors();
  ASSERT_EQ(result
                .run("\"$(read x <<$'\\x45OF'\nliteral )\nEOF\n"
                     "printf '%s' \"$x\")\"")
                .status,
            0);
  ASSERT_EQ(result.value.we_wordc, size_t(1));
  EXPECT_STREQ(result.value.we_wordv[0], "literal )");
  EXPECT_EQ(descriptors(), before);
  EXPECT_EQ(syscall_impl<long>(SYS_wait4, -1, nullptr, WNOHANG, nullptr),
            -long(ECHILD));
}

TEST(LlvmLibcWordRuntimeTest, ForbiddenCommandsDoNotPublishOrAssign) {
  auto &environment = EnvironmentManager::get_instance();
  environment.unset("LLVM_LIBC_WORD_ASSIGN");
  Result result;
  ASSERT_EQ(result.run("previous").status, 0);
  auto **previous = result.value.we_wordv;
  auto error = result.run("${LLVM_LIBC_WORD_ASSIGN:=changed} $(exit 7)",
                          WRDE_APPEND | WRDE_NOCMD);
  EXPECT_EQ(error.status, WRDE_CMDSUB);
  EXPECT_EQ(result.value.we_wordv, previous);
  EXPECT_EQ(result.value.we_wordc, size_t(1));
  EXPECT_EQ(environment.get("LLVM_LIBC_WORD_ASSIGN"),
            static_cast<char *>(nullptr));
  EXPECT_EQ(syscall_impl<long>(SYS_wait4, -1, nullptr, WNOHANG, nullptr),
            -long(ECHILD));
}

TEST(LlvmLibcWordRuntimeTest, EntrypointPreservesErrnoAndReportsActualCause) {
  Result result;
  libc_errno = EDOM;
  ASSERT_EQ(LIBC_NAMESPACE::wordexp("\"$(printf retained; exit 7)\"",
                                    &result.value, 0),
            0);
  EXPECT_EQ(int(libc_errno), EDOM);
  ASSERT_EQ(result.value.we_wordc, size_t(1));
  EXPECT_STREQ(result.value.we_wordv[0], "retained");
  auto **previous = result.value.we_wordv;
  EXPECT_EQ(LIBC_NAMESPACE::wordexp("|", &result.value, WRDE_APPEND),
            WRDE_BADCHAR);
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(result.value.we_wordv, previous);

  // This test's account provider deliberately fails with ENOENT.
  EXPECT_EQ(LIBC_NAMESPACE::wordexp("~llvm_libc_missing_account", &result.value,
                                    WRDE_APPEND),
            WRDE_SYNTAX);
  EXPECT_EQ(int(libc_errno), ENOENT);
  EXPECT_EQ(result.value.we_wordv, previous);
}

TEST(LlvmLibcWordRuntimeTest, EntrypointUsesSelectedCTypeForEncodingErrors) {
  auto old = thread_locale;
  auto utf8 = locale_from_mask(1 << LC_CTYPE);
  thread_locale = &utf8;
  Result result;
  libc_errno = EDOM;
  int status = LIBC_NAMESPACE::wordexp("\xff", &result.value, 0);
  thread_locale = old;
  EXPECT_EQ(status, WRDE_SYNTAX);
  EXPECT_EQ(int(libc_errno), EILSEQ);
  EXPECT_EQ(result.value.we_wordc, size_t(0));
  EXPECT_EQ(result.value.we_wordv, static_cast<char **>(nullptr));
}

TEST(LlvmLibcWordRuntimeTest, EntrypointMapsEachServiceFailureAndPrefix) {
  const int errors[] = {ENOMEM, EAGAIN, EMFILE, ENFILE, EILSEQ,
                        EIO,    ENOENT, EACCES, ENOTSUP};
  for (int error : errors) {
    Result result;
    ASSERT_EQ(result.run("old").status, 0);
    auto **previous = result.value.we_wordv;
    auto *old = previous[0];
    account_error = error;
    libc_errno = EDOM;
    int status = LIBC_NAMESPACE::wordexp("prefix ~llvm_libc_missing_account",
                                         &result.value, WRDE_APPEND);
    account_error = ENOENT;
    bool resource = error == ENOMEM || error == EAGAIN || error == EMFILE ||
                    error == ENFILE;
    EXPECT_EQ(status, resource ? WRDE_NOSPACE : WRDE_SYNTAX);
    EXPECT_EQ(int(libc_errno), error);
    EXPECT_EQ(result.value.we_wordc, size_t(resource ? 2 : 1));
    EXPECT_EQ(result.value.we_wordv[0], old);
    if (resource)
      EXPECT_STREQ(result.value.we_wordv[1], "prefix");
    else
      EXPECT_EQ(result.value.we_wordv, previous);
  }
}
