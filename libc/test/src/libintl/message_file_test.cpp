//===-- Message object file loading tests ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/message_file.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE;
using namespace LIBC_NAMESPACE::internal;

namespace {
struct Fixture {
  uint8_t bytes[64]{};
  size_t offset = 0, available = sizeof(bytes), chunk = sizeof(bytes);
  int error = 0;
  void put(size_t offset, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      bytes[offset + i] = uint8_t(value >> (8 * i));
  }
  Fixture() {
    put(0, 0x950412de);
    put(8, 1);
    put(12, 28);
    put(16, 36);
    put(28, 3);
    put(32, 44);
    put(36, 3);
    put(40, 48);
    bytes[44] = 'k';
    bytes[45] = 'e';
    bytes[46] = 'y';
    bytes[48] = 'v';
    bytes[49] = 'a';
    bytes[50] = 'l';
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
  auto load() {
    return read_message_file(sizeof(bytes), [&](uint8_t *out, size_t size) {
      return read(out, size);
    });
  }
};
} // namespace

TEST(LlvmLibcMessageFileTest, ChunkedReadsAndOwnedResult) {
  const size_t chunks[] = {1, 7, 64};
  for (size_t chunk : chunks) {
    Fixture f;
    f.chunk = chunk;
    auto result = f.load();
    ASSERT_TRUE(result.has_value());
    f.bytes[48] = 'X';
    auto found = (*result)->view().find("key");
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->translation == "val");
    (*result)->release();
  }
}

TEST(LlvmLibcMessageFileTest, TruncationGrowthAndMalformedInput) {
  Fixture f;
  --f.available;
  EXPECT_EQ(f.load().error(), EIO);
  f = Fixture();
  ++f.available;
  EXPECT_EQ(f.load().error(), EIO);
  f = Fixture();
  f.bytes[0] = 0;
  EXPECT_EQ(f.load().error(), EINVAL);
  auto excessive = read_message_file(
      64, [](uint8_t *, size_t size) { return ErrorOr<size_t>(size + 1); });
  EXPECT_EQ(excessive.error(), EIO);
}

TEST(LlvmLibcMessageFileTest, ErrorsAtEachReadAndBounds) {
  const int errors[] = {EIO, EINTR, EACCES};
  for (int error : errors) {
    Fixture f;
    f.error = error;
    EXPECT_EQ(f.load().error(), error);
    f.error = 0;
    auto end =
        read_message_file(sizeof(f.bytes), [&](uint8_t *out, size_t size) {
          return f.offset == sizeof(f.bytes) ? ErrorOr<size_t>(Error(error))
                                             : f.read(out, size);
        });
    EXPECT_EQ(end.error(), error);
  }
  const size_t sizes[] = {0, 27, MoCatalogView::MAX_SIZE + 1, size_t(-1)};
  for (size_t size : sizes) {
    bool called = false;
    auto result = read_message_file(size, [&](uint8_t *, size_t) {
      called = true;
      return ErrorOr<size_t>(0);
    });
    EXPECT_EQ(result.error(), EINVAL);
    EXPECT_FALSE(called);
  }
}

#ifdef MESSAGE_FILE_LINUX_TEST
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/stdlib/mkdtemp.h"
#include <sys/syscall.h>

TEST(LlvmLibcMessageFileTest, LinuxTransportRejectsNonRegularFiles) {
  char path[] = "message-file-XXXXXX";
  ASSERT_NE(LIBC_NAMESPACE::mkdtemp(path), nullptr);
  struct Root {
    const char *name;
    char filename[64]{};
    ~Root() {
      syscall_impl<long>(SYS_unlinkat, AT_FDCWD, filename, 0);
      syscall_impl<long>(SYS_unlinkat, AT_FDCWD, name, AT_REMOVEDIR);
    }
  } root{path};
  size_t size = 0;
  for (char c : cpp::string_view(path))
    root.filename[size++] = c;
  for (char c : cpp::string_view("/catalog.mo"))
    root.filename[size++] = c;
  EXPECT_EQ(load_message_file(path).error(), EINVAL);
  EXPECT_EQ(load_message_file(root.filename).error(), ENOENT);
  ASSERT_EQ(syscall_impl<long>(SYS_mknodat, AT_FDCWD, root.filename,
                               S_IFIFO | 0600, 0),
            0L);
  // No writer: O_NONBLOCK must prevent opening the FIFO from hanging.
  EXPECT_EQ(load_message_file(root.filename).error(), EINVAL);
  ASSERT_EQ(syscall_impl<long>(SYS_unlinkat, AT_FDCWD, root.filename, 0), 0L);
  int fd = syscall_impl<int>(SYS_openat, AT_FDCWD, root.filename,
                             O_WRONLY | O_CREAT, 0600);
  ASSERT_GE(fd, 0);
  Fixture f;
  EXPECT_EQ(syscall_impl<long>(SYS_write, fd, f.bytes, sizeof(f.bytes)),
            long(sizeof(f.bytes)));
  EXPECT_EQ(syscall_impl<long>(SYS_close, fd), 0L);
  auto loaded = load_message_file(root.filename);
  ASSERT_TRUE(loaded.has_value());
  auto message = (*loaded)->view().find("key");
  ASSERT_TRUE(message.has_value());
  EXPECT_TRUE(message->translation == "val");
  (*loaded)->release();
}
#endif

#ifdef MESSAGE_FILE_ALLOCATION_TEST
namespace {
bool tracking = false;
unsigned calls = 0, fail_call = 0, live = 0;
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++calls == fail_call)
    return nullptr;
  void *p = __real_malloc(size);
  if (tracking && p)
    ++live;
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (tracking && p)
    --live;
  __real_free(p);
}

TEST(LlvmLibcMessageFileTest, AllocationFailureReleasesPartialStorage) {
  for (unsigned failure = 1; failure != 4; ++failure) {
    Fixture f;
    calls = 0;
    fail_call = failure;
    tracking = true;
    auto result = f.load();
    tracking = false;
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ENOMEM);
    EXPECT_EQ(live, 0U);
  }
}
#endif
