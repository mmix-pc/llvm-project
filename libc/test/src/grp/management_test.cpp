//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/func/realloc.h"
#include "src/grp/getgrnam.h"
#include "src/grp/lookup.h"
#include "src/stdio/remove.h"
#include "test/UnitTest/Test.h"

// Keep real file parsing and entrypoint logic, but never change host groups.
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_SYSCALL_WRAPPERS_SETGROUPS_H
static unsigned install_calls;
static int install_error;
static size_t installed_count;
static gid_t installed_groups[128];
namespace LIBC_NAMESPACE_DECL {
namespace linux_syscalls {
ErrorOr<int> setgroups(size_t size, const gid_t *list) {
  ++install_calls;
  if (install_error)
    return Error(install_error);
  if (size > 128)
    return Error(EINVAL);
  installed_count = size;
  for (size_t i = 0; i < size; ++i)
    installed_groups[i] = list[i];
  return 0;
}
} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

static unsigned allocations, fail_allocation;
static void *group_realloc(void *ptr, size_t size) {
  if (++allocations == fail_allocation)
    return nullptr;
  return ::realloc(ptr, size);
}
// Only the group's growable ID array uses this allocation boundary. The shared
// record reader's own allocation tests remain in the account-query suite.
#define realloc group_realloc
#include "src/grp/linux/initgroups.cpp"
#undef realloc
#include "src/grp/endgrent.cpp"

TEST(LlvmLibcGroupManagementTest, MembershipAndLifetime) {
  const char *path = libc_make_test_file_path("group-management.test");
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  const char records[] =
      "# comment\ninvalid\nbase:x:7:alice\n"
      "staff:x:8:alice,bob\nduplicate:x:8:alice\nother:x:9:alice2\n"
      "empty:x:10:\nmalformed:x:11:alice,,bob\nlast:x:12:alice";
  ASSERT_EQ(file.value()->write(records, sizeof(records) - 1).value,
            sizeof(records) - 1);
  ASSERT_EQ(file.value()->close(), 0);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(path);
  install_calls = 0;
  install_error = 0;
  libc_errno = EDOM;
  ASSERT_EQ(LIBC_NAMESPACE::initgroups("alice", 7), 0);
  EXPECT_EQ(install_calls, 1U);
  ASSERT_EQ(installed_count, size_t(3));
  EXPECT_EQ(installed_groups[0], gid_t(7));
  EXPECT_EQ(installed_groups[1], gid_t(8));
  EXPECT_EQ(installed_groups[2], gid_t(12));
  EXPECT_EQ(int(libc_errno), EDOM);
  ASSERT_EQ(LIBC_NAMESPACE::initgroups("absent", 42), 0);
  ASSERT_EQ(installed_count, size_t(1));
  EXPECT_EQ(installed_groups[0], gid_t(42));

  ASSERT_TRUE(LIBC_NAMESPACE::getgrnam("staff") != nullptr);
  ASSERT_TRUE(LIBC_NAMESPACE::group_db::lookup_buffer.buffer != nullptr);
  libc_errno = ENOSYS;
  LIBC_NAMESPACE::endgrent();
  EXPECT_EQ(int(libc_errno), ENOSYS);
  EXPECT_EQ(LIBC_NAMESPACE::group_db::lookup_buffer.buffer,
            static_cast<char *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::group_db::lookup_buffer.size, size_t(0));
  LIBC_NAMESPACE::endgrent();
  EXPECT_EQ(int(libc_errno), ENOSYS);
  ASSERT_TRUE(LIBC_NAMESPACE::getgrnam("staff") != nullptr);
  LIBC_NAMESPACE::endgrent();

  const int errors[] = {EPERM, EINVAL, ENOSYS, ENOMEM};
  for (int error : errors) {
    install_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::initgroups("alice", 7), -1);
    EXPECT_EQ(int(libc_errno), error);
    LIBC_NAMESPACE::endgrent();
    EXPECT_EQ(int(libc_errno), error);
  }
  install_error = 0;
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
  install_calls = 0;
  EXPECT_EQ(LIBC_NAMESPACE::initgroups("alice", 7), -1);
  EXPECT_EQ(int(libc_errno), ENOENT);
  EXPECT_EQ(install_calls, 0U);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(nullptr);
}

TEST(LlvmLibcGroupManagementTest, InvalidInputsAndReadFailure) {
  install_calls = 0;
  EXPECT_EQ(LIBC_NAMESPACE::initgroups(nullptr, 1), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::initgroups("", 1), -1);
  EXPECT_EQ(LIBC_NAMESPACE::initgroups("alice", gid_t(-1)), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(".");
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(LIBC_NAMESPACE::initgroups("alice", 1), -1);
    EXPECT_EQ(int(libc_errno), EISDIR);
  }
  EXPECT_EQ(install_calls, 0U);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(nullptr);
}

TEST(LlvmLibcGroupManagementTest, GrowthAndAllocationFailure) {
  const char *path = libc_make_test_file_path("group-management-large.test");
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  for (int i = 10; i < 40; ++i) {
    char line[] = "g:x:00:alice\n";
    line[4] = static_cast<char>('0' + i / 10);
    line[5] = static_cast<char>('0' + i % 10);
    ASSERT_EQ(file.value()->write(line, sizeof(line) - 1).value,
              sizeof(line) - 1);
  }
  const char start[] = "long:x:99:";
  ASSERT_EQ(file.value()->write(start, sizeof(start) - 1).value,
            sizeof(start) - 1);
  for (int i = 0; i < 512; ++i)
    ASSERT_EQ(file.value()->write("x", 1).value, size_t(1));
  ASSERT_EQ(file.value()->write(",alice\n", 7).value, size_t(7));
  ASSERT_EQ(file.value()->close(), 0);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(path);
  fail_allocation = 0;
  allocations = 0;
  install_calls = 0;
  ASSERT_EQ(LIBC_NAMESPACE::initgroups("alice", 7), 0);
  EXPECT_EQ(install_calls, 1U);
  ASSERT_EQ(installed_count, size_t(32));
  EXPECT_EQ(installed_groups[0], gid_t(7));
  EXPECT_EQ(installed_groups[31], gid_t(99));
  for (unsigned fail = 1; fail <= 2; ++fail) {
    allocations = 0;
    fail_allocation = fail;
    install_calls = 0;
    EXPECT_EQ(LIBC_NAMESPACE::initgroups("alice", 7), -1);
    EXPECT_EQ(int(libc_errno), ENOMEM);
    EXPECT_EQ(install_calls, 0U);
  }
  fail_allocation = 0;
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(nullptr);
}

TEST(LlvmLibcGroupManagementTest, KernelLimitAndInvalidGroup) {
  LIBC_NAMESPACE::GroupList groups;
  groups.data =
      static_cast<gid_t *>(::realloc(nullptr, NGROUPS_MAX * sizeof(gid_t)));
  ASSERT_TRUE(groups.data != nullptr);
  groups.size = groups.capacity = NGROUPS_MAX;
  for (size_t i = 0; i < groups.size; ++i)
    groups.data[i] = static_cast<gid_t>(i);
  EXPECT_EQ(groups.append(7), 0);
  EXPECT_EQ(groups.append(gid_t(NGROUPS_MAX)), EINVAL);
  EXPECT_EQ(groups.append(gid_t(-1)), EINVAL);
}

TEST(LlvmLibcGroupManagementTest, InvalidMatchedGroup) {
  const char *path = libc_make_test_file_path("group-invalid-id.test");
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  const char record[] = "invalid:x:4294967295:alice\n";
  ASSERT_EQ(file.value()->write(record, sizeof(record) - 1).value,
            sizeof(record) - 1);
  ASSERT_EQ(file.value()->close(), 0);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(path);
  install_calls = 0;
  install_error = 0;
  EXPECT_EQ(LIBC_NAMESPACE::initgroups("alice", 7), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(install_calls, 0U);
  EXPECT_EQ(LIBC_NAMESPACE::initgroups("bob", 7), 0);
  EXPECT_EQ(install_calls, 1U);
  EXPECT_EQ(installed_count, size_t(1));
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
  LIBC_NAMESPACE::group_db::TESTONLY_set_lookup_path(nullptr);
}
