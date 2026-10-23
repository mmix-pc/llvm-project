//===-- Tests for Linux statx to stat conversion --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/types/struct_stat.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

using KernelStat = LIBC_NAMESPACE::internal::kernel_statx_buf;
using Stat = struct stat;

namespace {
KernelStat reply;
int syscall_result, calls;
constexpr int FD = 37;
constexpr int FLAGS = AT_SYMLINK_NOFOLLOW;
const char PATH[] = "relative/metadata";

void reset() {
  reply = {};
  reply.stx_mask = LIBC_NAMESPACE::internal::KERNEL_STATX_BASIC_STATS_MASK;
  syscall_result = 0;
  calls = 0;
}

void expect_untouched(const Stat &output) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(&output);
  for (unsigned long i = 0; i < sizeof(output); ++i)
    EXPECT_EQ(bytes[i], static_cast<unsigned char>(0xa5));
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T>
T statx_test_syscall(long number, int fd, const char *path, int flags,
                     unsigned int mask, void *buffer) {
  EXPECT_EQ(number, long(SYS_statx));
  EXPECT_EQ(fd, FD);
  EXPECT_EQ(path, PATH);
  EXPECT_EQ(flags, FLAGS);
  EXPECT_EQ(mask, internal::KERNEL_STATX_BASIC_STATS_MASK);
  ++calls;
  *static_cast<KernelStat *>(buffer) = reply;
  return static_cast<T>(syscall_result);
}
} // namespace LIBC_NAMESPACE_DECL

// Inject only the provider's statx transport, not the test runner's syscalls.
#define syscall_impl statx_test_syscall
#include "src/__support/OSUtil/linux/stat/stat_via_statx.h"
#undef syscall_impl

namespace {
auto query(Stat &output) {
  return LIBC_NAMESPACE::internal::stat_via_statx(FD, PATH, FLAGS, &output);
}

template <typename T>
void check_field_range(uint64_t KernelStat::*kernel_field,
                       T Stat::*public_field) {
  constexpr uint64_t MAX = LIBC_NAMESPACE::cpp::numeric_limits<T>::max();
  constexpr uint64_t KERNEL_MAX =
      LIBC_NAMESPACE::cpp::numeric_limits<uint64_t>::max();
  constexpr uint64_t VALID[] = {0, MAX};
  for (uint64_t value : VALID) {
    reset();
    reply.*kernel_field = value;
    Stat output;
    __builtin_memset(&output, 0xa5, sizeof(output));
    auto result = query(output);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(output.*public_field, static_cast<T>(value));
  }

  // Synthetic conversion boundaries, not claims about creatable files. A
  // full-width unsigned inode has no overflowing statx value to inject.
  if constexpr (MAX < KERNEL_MAX) {
    constexpr uint64_t OVERFLOW[] = {MAX + 1, KERNEL_MAX};
    for (uint64_t value : OVERFLOW) {
      reset();
      reply.*kernel_field = value;
      Stat output;
      __builtin_memset(&output, 0xa5, sizeof(output));
      auto result = query(output);
      ASSERT_FALSE(result.has_value());
      EXPECT_EQ(result.error(), EOVERFLOW);
      EXPECT_EQ(calls, 1);
      expect_untouched(output);
    }
  }
}
} // namespace

TEST(LlvmLibcStatViaStatxTest, DeviceEncoding) {
  struct Device {
    uint32_t major;
    uint32_t minor;
    uint64_t encoded;
  };
  constexpr Device DEVICES[] = {
      {0, 0, 0},
      {1, 3, 0x103},
      {0, 255, 0xff},
      {0, 256, 0x100000},
      {1, 0, 0x100},
      {0xfff, 0xff, 0xfffff},
      {0x1000, 0, 0x100000000000ULL},
      {0xffffffff, 0xffffffff, 0xffffffffffffffffULL},
  };
  constexpr unsigned COUNT = sizeof(DEVICES) / sizeof(DEVICES[0]);
  for (unsigned i = 0; i < COUNT; ++i) {
    reset();
    const Device &device = DEVICES[i];
    const Device &rdevice = DEVICES[(i + 1) % COUNT];
    reply.stx_dev_major = device.major;
    reply.stx_dev_minor = device.minor;
    reply.stx_rdev_major = rdevice.major;
    reply.stx_rdev_minor = rdevice.minor;
    Stat output{};
    ASSERT_TRUE(query(output).has_value());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(output.st_dev,
              static_cast<decltype(output.st_dev)>(device.encoded));
    EXPECT_EQ(output.st_rdev,
              static_cast<decltype(output.st_rdev)>(rdevice.encoded));
  }
}

TEST(LlvmLibcStatViaStatxTest, MetadataFields) {
  reset();
  reply.stx_ino = 12345;
  reply.stx_mode = 0100640;
  reply.stx_nlink = 3;
  reply.stx_uid = 123;
  reply.stx_gid = 456;
  reply.stx_size = 8193;
  reply.stx_blocks = 24;
  reply.stx_blksize = 4096;
  reply.stx_atime = {-123, 456, 0};
  reply.stx_mtime = {789, 123456789, 0};
  reply.stx_ctime = {987, 999999999, 0};
  Stat output{};
  ASSERT_TRUE(query(output).has_value());
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(output.st_ino, decltype(output.st_ino)(12345));
  EXPECT_EQ(output.st_mode, decltype(output.st_mode)(0100640));
  EXPECT_EQ(output.st_nlink, decltype(output.st_nlink)(3));
  EXPECT_EQ(output.st_uid, decltype(output.st_uid)(123));
  EXPECT_EQ(output.st_gid, decltype(output.st_gid)(456));
  EXPECT_EQ(output.st_size, decltype(output.st_size)(8193));
  EXPECT_EQ(output.st_blocks, decltype(output.st_blocks)(24));
  EXPECT_EQ(output.st_blksize, decltype(output.st_blksize)(4096));
  EXPECT_EQ(output.st_atim.tv_sec, decltype(output.st_atim.tv_sec)(-123));
  EXPECT_EQ(output.st_atim.tv_nsec, decltype(output.st_atim.tv_nsec)(456));
  EXPECT_EQ(output.st_mtim.tv_sec, decltype(output.st_mtim.tv_sec)(789));
  EXPECT_EQ(output.st_mtim.tv_nsec,
            decltype(output.st_mtim.tv_nsec)(123456789));
  EXPECT_EQ(output.st_ctim.tv_sec, decltype(output.st_ctim.tv_sec)(987));
  EXPECT_EQ(output.st_ctim.tv_nsec,
            decltype(output.st_ctim.tv_nsec)(999999999));
}

TEST(LlvmLibcStatViaStatxTest, InodeRange) {
  check_field_range(&KernelStat::stx_ino, &Stat::st_ino);
}

TEST(LlvmLibcStatViaStatxTest, SizeRange) {
  check_field_range(&KernelStat::stx_size, &Stat::st_size);
}

TEST(LlvmLibcStatViaStatxTest, BlockCountRange) {
  check_field_range(&KernelStat::stx_blocks, &Stat::st_blocks);
}

TEST(LlvmLibcStatViaStatxTest, SyscallErrorsLeaveOutputUntouched) {
  const int errors[] = {EBADF, EACCES, ENOENT, EIO, EINTR, EOVERFLOW};
  for (int error : errors) {
    reset();
    syscall_result = -error;
    Stat output;
    __builtin_memset(&output, 0xa5, sizeof(output));
    auto result = query(output);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error);
    EXPECT_EQ(calls, 1);
    expect_untouched(output);
  }
}
