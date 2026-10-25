//===-- Bounded locale file loading tests ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/locale_file.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
// Deliberately empty categories: loading is not semantic publication.
struct Fixture {
  static constexpr char NAME[] = "tr_TR.UTF-8";
  uint8_t bytes[128 + sizeof(NAME)]{};
  size_t offset = 0, chunk = sizeof(bytes), available = sizeof(bytes);
  int error = 0;
  static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - 8 * i));
  }
  Fixture() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != sizeof(magic); ++i)
      bytes[i] = magic[i];
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 16, sizeof(bytes));
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, sizeof(NAME));
    for (unsigned i = 0; i != sizeof(NAME); ++i)
      bytes[128 + i] = NAME[i];
    for (unsigned i = 0; i != 6; ++i) {
      put(bytes + 32 + 16 * i, i);
      put(bytes + 36 + 16 * i, sizeof(bytes));
    }
  }
  ErrorOr<size_t> read(uint8_t *out, size_t size) {
    if (error)
      return Error(error);
    size_t count = available - offset;
    if (count > size)
      count = size;
    if (count > chunk)
      count = chunk;
    for (size_t i = 0; i != count; ++i)
      out[i] = offset + i < sizeof(bytes) ? bytes[offset + i] : 0;
    offset += count;
    return count;
  }
  ErrorOr<LocaleDataFile *> load(const char *name = NAME) {
    return read_locale_file(
        sizeof(bytes), name,
        [&](uint8_t *out, size_t size) { return read(out, size); });
  }
};
} // namespace

TEST(LlvmLibcLocaleFileTest, ControlledNames) {
  EXPECT_STREQ(locale_file_name("tr_TR.UTF-8"), "tr_TR.UTF-8");
  EXPECT_STREQ(locale_file_name("de_DE.UTF-8"), "de_DE.UTF-8");
  EXPECT_STREQ(locale_file_name("en_US.UTF-8"), "en_US.UTF-8");
  const char *names[] = {"",
                         "C",
                         "C.UTF-8",
                         "../tr_TR.UTF-8",
                         "tr_TR.UTF-8/locale.dat",
                         "/etc/passwd",
                         "unknown"};
  for (const char *name : names)
    EXPECT_EQ(locale_file_name(name), nullptr);
}

TEST(LlvmLibcLocaleFileTest, PartialReadsAndFrozenCopy) {
  const size_t chunks[] = {1, 7, 140};
  for (size_t chunk : chunks) {
    Fixture f;
    f.chunk = chunk;
    auto file = f.load();
    ASSERT_TRUE(file.has_value());
    f.bytes[128] = 'X';
    EXPECT_STREQ((*file)->view().name(), Fixture::NAME);
    (*file)->release();
  }
}

TEST(LlvmLibcLocaleFileTest, TruncationGrowthIdentityAndMalformedData) {
  Fixture f;
  --f.available;
  EXPECT_EQ(f.load().error(), EIO);
  f = Fixture();
  ++f.available;
  EXPECT_EQ(f.load().error(), EIO);
  f = Fixture();
  EXPECT_EQ(f.load("en_US.UTF-8").error(), EINVAL);
  f = Fixture();
  f.bytes[8] = 1;
  EXPECT_EQ(f.load().error(), EINVAL);
}

TEST(LlvmLibcLocaleFileTest, ReadErrorsAndBounds) {
  const int errors[] = {EIO, EINTR, EACCES};
  for (int error : errors) {
    Fixture f;
    f.error = error;
    EXPECT_EQ(f.load().error(), error);
    auto result =
        read_locale_file(sizeof(f.bytes), Fixture::NAME,
                         [&](uint8_t *out, size_t size) -> ErrorOr<size_t> {
                           if (f.offset == sizeof(f.bytes))
                             return Error(error);
                           f.error = 0;
                           return f.read(out, size);
                         });
    EXPECT_EQ(result.error(), error);
  }
  const size_t sizes[] = {0, 127, LocaleDataView::MAX_SIZE + 1};
  for (size_t size : sizes) {
    bool called = false;
    auto result = read_locale_file(size, Fixture::NAME,
                                   [&](uint8_t *, size_t) -> ErrorOr<size_t> {
                                     called = true;
                                     return 0;
                                   });
    EXPECT_EQ(result.error(), EINVAL);
    EXPECT_FALSE(called);
  }
}

#ifdef LOCALE_FILE_LINUX_TEST
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/stdlib/mkdtemp.h"
#include <sys/syscall.h>

TEST(LlvmLibcLocaleFileTest, LinuxTransportAndNoPathTraversal) {
  using LIBC_NAMESPACE::syscall_impl;
  char path[] = "locale-files-XXXXXX";
  ASSERT_NE(LIBC_NAMESPACE::mkdtemp(path), nullptr);
  struct Root {
    int fd;
    const char *path;
    ~Root() {
      syscall_impl<long>(SYS_unlinkat, fd, "tr_TR.UTF-8/locale.dat", 0);
      syscall_impl<long>(SYS_unlinkat, fd, "tr_TR.UTF-8", AT_REMOVEDIR);
      syscall_impl<long>(SYS_unlinkat, fd, "de_DE.UTF-8", 0);
      syscall_impl<long>(SYS_close, fd);
      syscall_impl<long>(SYS_unlinkat, AT_FDCWD, path, AT_REMOVEDIR);
    }
  } root{int(syscall_impl<long>(SYS_openat, AT_FDCWD, path,
                                O_RDONLY | O_DIRECTORY, 0)),
         path};
  ASSERT_GE(root.fd, 0);
  EXPECT_EQ(load_locale_file_at(root.fd, "../tr_TR.UTF-8").error(), ENOENT);
  EXPECT_EQ(load_locale_file_at(root.fd, "tr_TR.UTF-8").error(), ENOENT);
  ASSERT_EQ(syscall_impl<long>(SYS_mkdirat, root.fd, "tr_TR.UTF-8", 0700), 0L);
  const char *filename = "tr_TR.UTF-8/locale.dat";
  int fd = syscall_impl<int>(SYS_openat, root.fd, filename, O_WRONLY | O_CREAT,
                             0600);
  ASSERT_GE(fd, 0);
  Fixture f;
  EXPECT_EQ(syscall_impl<long>(SYS_write, fd, f.bytes, sizeof(f.bytes)),
            long(sizeof(f.bytes)));
  EXPECT_EQ(syscall_impl<long>(SYS_close, fd), 0L);
  auto loaded = load_locale_file_at(root.fd, Fixture::NAME);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_STREQ((*loaded)->view().name(), Fixture::NAME);
  (*loaded)->release();
  ASSERT_EQ(
      syscall_impl<long>(SYS_symlinkat, "tr_TR.UTF-8", root.fd, "de_DE.UTF-8"),
      0L);
  EXPECT_FALSE(load_locale_file_at(root.fd, "de_DE.UTF-8").has_value());
  ASSERT_EQ(syscall_impl<long>(SYS_unlinkat, root.fd, filename, 0), 0L);
  ASSERT_EQ(syscall_impl<long>(SYS_symlinkat, "/dev/null", root.fd, filename),
            0L);
  EXPECT_EQ(load_locale_file_at(root.fd, Fixture::NAME).error(), ELOOP);
  ASSERT_EQ(syscall_impl<long>(SYS_unlinkat, root.fd, filename, 0), 0L);
  ASSERT_EQ(
      syscall_impl<long>(SYS_mknodat, root.fd, filename, S_IFIFO | 0600, 0),
      0L);
  EXPECT_EQ(load_locale_file_at(root.fd, Fixture::NAME).error(), EINVAL);
}
#endif

#ifdef LOCALE_FILE_ALLOCATION_TEST
namespace {
bool tracking = false;
unsigned calls = 0, fail_call = 0, live = 0;
void *allocations[8]{};
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++calls == fail_call)
    return nullptr;
  void *p = __real_malloc(size);
  if (tracking && p)
    for (void *&slot : allocations)
      if (!slot) {
        slot = p;
        ++live;
        break;
      }
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (p)
    for (void *&slot : allocations)
      if (slot == p) {
        slot = nullptr;
        --live;
        break;
      }
  __real_free(p);
}

TEST(LlvmLibcLocaleFileTest, AllocationFailureAndInputErrorRelease) {
  for (unsigned failure = 1; failure != 4; ++failure) {
    Fixture f;
    calls = 0;
    fail_call = failure;
    tracking = true;
    auto file = f.load();
    tracking = false;
    EXPECT_FALSE(file.has_value());
    EXPECT_EQ(file.error(), ENOMEM);
    EXPECT_EQ(live, 0U);
  }
  for (unsigned error = 0; error != 4; ++error) {
    Fixture f;
    if (error == 1)
      --f.available;
    if (error == 2)
      f.error = EIO;
    calls = fail_call = 0;
    tracking = true;
    auto file = f.load(error == 3 ? "wrong" : Fixture::NAME);
    tracking = false;
    if (error) {
      EXPECT_FALSE(file.has_value());
    } else {
      ASSERT_TRUE(file.has_value());
      EXPECT_EQ(live, 2U);
      (*file)->release();
    }
    EXPECT_EQ(live, 0U);
  }
}
#endif
