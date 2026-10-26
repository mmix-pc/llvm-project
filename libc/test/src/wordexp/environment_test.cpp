//===-- Word expansion environment tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/app.h"
#include "hdr/errno_macros.h"
#include "src/pwd/getpwnam_r.h"
#include "src/stdlib/environ_internal.h"
#include "src/wordexp/environment.h"
#include "test/UnitTest/Test.h"

namespace {
int account_calls = 0;
int account_error = 0;
} // namespace

namespace LIBC_NAMESPACE_DECL {
AppProperties app;

// Exercise the real environment adapter's buffer/error handling independently
// of the machine's account database. Target account I/O is a separate test.
int getpwnam_r(const char *name, struct passwd *entry, char *buffer,
               size_t size, struct passwd **result) {
  ++account_calls;
  *result = nullptr;
  if (account_error)
    return account_error;
  if (cpp::string_view(name) != "user")
    return 0;
  if (size < 4096)
    return ERANGE;
  constexpr char home[] = "/home/user";
  for (size_t i = 0; i < sizeof(home); ++i)
    buffer[i] = home[i];
  entry->pw_dir = buffer;
  *result = entry;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL

using namespace LIBC_NAMESPACE::internal;

TEST(LlvmLibcWordEnvironmentTest, LookupAndAssignmentUseActualEnvironment) {
  auto &environment = EnvironmentManager::get_instance();
  environment.unset("LLVM_LIBC_WORD_TEST");
  WordEnvironment context;
  auto services = context.services();
  auto read = services.lookup(services.context, "LLVM_LIBC_WORD_TEST");
  ASSERT_TRUE(read.has_value());
  EXPECT_FALSE(read->set);
  ASSERT_EQ(services.assign(services.context, "LLVM_LIBC_WORD_TEST", "a b"), 0);
  read = services.lookup(services.context, "LLVM_LIBC_WORD_TEST");
  ASSERT_TRUE(read.has_value());
  EXPECT_TRUE(read->set);
  EXPECT_TRUE(read->value == "a b");
  EXPECT_STREQ(environment.get("LLVM_LIBC_WORD_TEST"), "a b");
  ASSERT_EQ(services.assign(services.context, "LLVM_LIBC_WORD_TEST", ""), 0);
  read = services.lookup(services.context, "LLVM_LIBC_WORD_TEST");
  ASSERT_TRUE(read.has_value());
  EXPECT_TRUE(read->set);
  EXPECT_TRUE(read->value.empty());
  EXPECT_EQ(environment.unset("LLVM_LIBC_WORD_TEST"), 0);
}

TEST(LlvmLibcWordEnvironmentTest, IntegerConstantsAndRealAssignment) {
  WordEnvironment context;
  auto services = context.services();
  auto variables = services.arithmetic;
  const char *valid[] = {"", " \t", "010", "0x10", "-0X10", " +42 "};
  const long expected[] = {0, 0, 8, 16, -16, 42};
  for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); ++i) {
    ASSERT_EQ(services.assign(services.context, "LLVM_LIBC_WORD_NUM", valid[i]),
              0);
    auto value = variables.read(variables.context, "LLVM_LIBC_WORD_NUM");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, expected[i]);
  }
  const char *bad[] = {"+", "08", "0b10", "1+2", "name", "0x"};
  for (const char *text : bad) {
    ASSERT_EQ(services.assign(services.context, "LLVM_LIBC_WORD_NUM", text), 0);
    auto value = variables.read(variables.context, "LLVM_LIBC_WORD_NUM");
    ASSERT_FALSE(value.has_value());
    EXPECT_EQ(value.error(), EINVAL);
  }
  auto result = evaluate_wordexp_arithmetic("LLVM_LIBC_WORD_NUM=12", variables);
  ASSERT_TRUE(result.has_value());
  EXPECT_STREQ(EnvironmentManager::get_instance().get("LLVM_LIBC_WORD_NUM"),
               "12");
  EXPECT_EQ(EnvironmentManager::get_instance().unset("LLVM_LIBC_WORD_NUM"), 0);
}

TEST(LlvmLibcWordEnvironmentTest, HomeUsesReentrantLookupAndRetainsErrors) {
  account_calls = 0;
  account_error = 0;
  WordEnvironment context;
  auto services = context.services();
  auto found = services.home(services.context, "user");
  ASSERT_TRUE(found.has_value());
  EXPECT_TRUE(found->set);
  EXPECT_TRUE(found->value == "/home/user");
  EXPECT_EQ(account_calls, 3);
  found = services.home(services.context, "missing");
  ASSERT_TRUE(found.has_value());
  EXPECT_FALSE(found->set);
  account_error = EIO;
  found = services.home(services.context, "user");
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error(), EIO);
  account_error = 0;
  found = services.home(services.context, "user");
  ASSERT_TRUE(found.has_value());
  EXPECT_TRUE(found->value == "/home/user");
}
