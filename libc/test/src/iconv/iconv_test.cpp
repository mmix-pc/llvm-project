//===-- Public iconv tests ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/libc_errno.h"
#include "src/iconv/iconv.h"
#include "src/iconv/iconv_close.h"
#include "src/iconv/iconv_open.h"
#include "src/locale/locale.h"
#include "test/UnitTest/Test.h"

namespace {
int fail_after = -1;
int live_allocations = 0;
struct Descriptor {
  iconv_t value;
  ~Descriptor() {
    if (value != reinterpret_cast<iconv_t>(-1))
      LIBC_NAMESPACE::iconv_close(value);
  }
};
struct Selection {
  locale_t old = LIBC_NAMESPACE::internal::thread_locale;
  __locale_t loc;
  explicit Selection(unsigned mask)
      : loc(LIBC_NAMESPACE::internal::locale_from_mask(mask)) {
    LIBC_NAMESPACE::internal::thread_locale = &loc;
  }
  ~Selection() { LIBC_NAMESPACE::internal::thread_locale = old; }
};
} // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (fail_after == 0)
    return nullptr;
  if (fail_after > 0)
    --fail_after;
  void *result = __real_malloc(size);
  live_allocations += result != nullptr;
  return result;
}
extern "C" void __wrap_free(void *ptr) {
  live_allocations -= ptr != nullptr;
  __real_free(ptr);
}

TEST(LlvmLibcIconvTest, EmptyNamesCaptureEncodingNotLaterSelection) {
  Selection selection(0);
  Descriptor descriptor{LIBC_NAMESPACE::iconv_open("UTF-8", "")};
  ASSERT_NE(descriptor.value, reinterpret_cast<iconv_t>(-1));
  selection.loc = LIBC_NAMESPACE::internal::locale_from_mask(1 << LC_CTYPE);
  char source[] = "\xe4";
  char buffer[8]{};
  char *in = source, *out = buffer;
  size_t left = 1, space = sizeof(buffer);
  libc_errno = 17;
  EXPECT_EQ(LIBC_NAMESPACE::iconv(descriptor.value, &in, &left, &out, &space),
            size_t(0));
  EXPECT_EQ(int(libc_errno), 17);
  EXPECT_EQ(left, size_t(0));
  EXPECT_STREQ(buffer, "\xc3\xa4");
}

TEST(LlvmLibcIconvTest, ErrorProgressAndRetry) {
  Descriptor descriptor{LIBC_NAMESPACE::iconv_open("UTF-16LE", "UTF-8")};
  ASSERT_NE(descriptor.value, reinterpret_cast<iconv_t>(-1));
  char source[] = "A\xe2\x82\xac";
  char buffer[8]{};
  char *in = source, *out = buffer;
  size_t left = 4, space = 2;
  EXPECT_EQ(LIBC_NAMESPACE::iconv(descriptor.value, &in, &left, &out, &space),
            size_t(-1));
  EXPECT_EQ(int(libc_errno), E2BIG);
  EXPECT_EQ(in, source + 1);
  EXPECT_EQ(out, buffer + 2);
  EXPECT_EQ(left, size_t(3));
  space = 2;
  EXPECT_EQ(LIBC_NAMESPACE::iconv(descriptor.value, &in, &left, &out, &space),
            size_t(0));
  EXPECT_EQ(static_cast<unsigned char>(buffer[2]), static_cast<unsigned char>(0xac));
  EXPECT_EQ(static_cast<unsigned char>(buffer[3]), static_cast<unsigned char>(0x20));
  char partial[] = "B\xe2\x82";
  in = partial;
  left = 3;
  out = buffer;
  space = 8;
  EXPECT_EQ(LIBC_NAMESPACE::iconv(descriptor.value, &in, &left, &out, &space),
            size_t(-1));
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(in, partial + 1);
  EXPECT_EQ(left, size_t(2));
}

TEST(LlvmLibcIconvTest, BuiltinTransliterationAndReset) {
  Selection selection(0);
  Descriptor descriptor{LIBC_NAMESPACE::iconv_open("ASCII//TRANSLIT", "UTF-8")};
  ASSERT_NE(descriptor.value, reinterpret_cast<iconv_t>(-1));
  const unsigned masks[] = {0U, 1U << LC_CTYPE};
  for (unsigned mask : masks) {
    selection.loc = LIBC_NAMESPACE::internal::locale_from_mask(mask);
    char source[] = "\xc3\xa4\xc2\xa9";
    char buffer[8]{};
    char *in = source, *out = buffer;
    size_t left = 4, space = sizeof(buffer);
    EXPECT_EQ(LIBC_NAMESPACE::iconv(descriptor.value, &in, &left, &out, &space),
              size_t(2));
    EXPECT_STREQ(buffer, "a(C)");
    EXPECT_EQ(LIBC_NAMESPACE::iconv(descriptor.value, nullptr, nullptr, &out,
                                   &space), size_t(0));
    EXPECT_EQ(LIBC_NAMESPACE::iconv(descriptor.value, nullptr, nullptr, nullptr,
                                   nullptr), size_t(0));
    in = nullptr;
    EXPECT_EQ(LIBC_NAMESPACE::iconv(descriptor.value, &in, nullptr, nullptr,
                                   nullptr), size_t(0));
  }
}

TEST(LlvmLibcIconvTest, InvalidNamesDescriptorsAndArguments) {
  const char *names[] = {"invalid", "UTF-8//", "UTF-8//IGNORE//TRANSLIT"};
  for (const char *name : names) {
    EXPECT_EQ(LIBC_NAMESPACE::iconv_open(name, "UTF-8"),
              reinterpret_cast<iconv_t>(-1));
    EXPECT_EQ(int(libc_errno), EINVAL);
  }
  EXPECT_EQ(LIBC_NAMESPACE::iconv_open("UTF-8", "ASCII//TRANSLIT"),
            reinterpret_cast<iconv_t>(-1));
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::iconv_close(reinterpret_cast<iconv_t>(-1)), -1);
  EXPECT_EQ(int(libc_errno), EBADF);
  EXPECT_EQ(LIBC_NAMESPACE::iconv(nullptr, nullptr, nullptr, nullptr, nullptr),
            size_t(-1));
  EXPECT_EQ(int(libc_errno), EBADF);
  Descriptor descriptor{LIBC_NAMESPACE::iconv_open("UTF-8", "UTF-8")};
  ASSERT_NE(descriptor.value, reinterpret_cast<iconv_t>(-1));
  char source[] = "A";
  char *in = source;
  size_t left = 1;
  EXPECT_EQ(LIBC_NAMESPACE::iconv(descriptor.value, &in, &left, nullptr, nullptr),
            size_t(-1));
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(in, source);
  EXPECT_EQ(left, size_t(1));
}

TEST(LlvmLibcIconvTest, AllocationFailureUnwindsDescriptor) {
  int baseline = live_allocations;
  for (int fail = 0; fail != 2; ++fail) {
    fail_after = fail;
    iconv_t descriptor = LIBC_NAMESPACE::iconv_open("ASCII//TRANSLIT", "UTF-8");
    fail_after = -1;
    EXPECT_EQ(descriptor, reinterpret_cast<iconv_t>(-1));
    EXPECT_EQ(int(libc_errno), ENOMEM);
    EXPECT_EQ(live_allocations, baseline);
  }
  {
    Descriptor descriptor{LIBC_NAMESPACE::iconv_open("UTF-8", "UTF-8")};
    ASSERT_NE(descriptor.value, reinterpret_cast<iconv_t>(-1));
  }
  EXPECT_EQ(live_allocations, baseline);
}
