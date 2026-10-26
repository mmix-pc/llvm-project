//===-- Message selection and recoding tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/libintl/message_translation.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE;
using internal::MoCatalogView;
using internal::translate_message;

namespace {
struct Fixture {
  uint8_t bytes[2048]{};
  size_t size = 28 + 4 * 16;
  bool big;
  void put(size_t position, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      bytes[position + (big ? 3 - i : i)] = uint8_t(value >> (8 * i));
  }
  void add(size_t table, size_t index, cpp::string_view value) {
    put(table + index * 8, uint32_t(value.size()));
    put(table + index * 8 + 4, uint32_t(size));
    for (char c : value)
      bytes[size++] = uint8_t(c);
    bytes[size++] = 0;
  }
  Fixture(bool big = false,
          cpp::string_view header =
              "Content-Type: text/plain; charset=ISO-8859-1\n"
              "Plural-Forms: nplurals=3; plural=(n==1?0:n==2?1:2);\n",
          cpp::string_view forms = {"un\0deux\0plus", 12})
      : big(big) {
    put(0, 0x950412de);
    put(8, 4);
    put(12, 28);
    put(16, 60);
    add(28, 0, "");
    add(60, 0, header);
    add(28, 1, {"apple\0apples", 12});
    add(60, 1, forms);
    add(28, 2, "coffee");
    add(60, 2, "caf\xe9");
    add(28, 3, "long");
    char long_value[100];
    for (char &c : long_value)
      c = '\xe9';
    add(60, 3, {long_value, sizeof(long_value)});
  }
  auto view() { return MoCatalogView::parse({bytes, size}); }
};
} // namespace

TEST(LlvmLibcMessageTranslationTest, SingularPluralAndBothByteOrders) {
  for (unsigned big = 0; big != 2; ++big) {
    Fixture f(big);
    auto view = f.view();
    ASSERT_TRUE(view.has_value());
    const char *expected[] = {"plus", "un", "deux", "plus"};
    for (unsigned long n = 0; n != 4; ++n) {
      auto result = translate_message(*view, "apple", n, "UTF-8");
      ASSERT_TRUE(result.has_value());
      EXPECT_STREQ(*result, expected[n]);
      ::free(*result);
    }
    auto singular = translate_message(*view, "apple", cpp::nullopt, "UTF-8");
    ASSERT_TRUE(singular.has_value());
    EXPECT_STREQ(*singular, "un");
    ::free(*singular);
    EXPECT_EQ(translate_message(*view, "absent", cpp::nullopt, "UTF-8").error(),
              ENOENT);
  }
}

TEST(LlvmLibcMessageTranslationTest, CodesetsAndIndependentResults) {
  Fixture f;
  auto view = f.view();
  ASSERT_TRUE(view.has_value());
  auto utf8 = translate_message(*view, "coffee", cpp::nullopt, "UTF-8");
  auto latin = translate_message(*view, "coffee", cpp::nullopt, "ISO-8859-1");
  auto ascii = translate_message(*view, "coffee", cpp::nullopt, "ASCII");
  ASSERT_TRUE(utf8.has_value());
  ASSERT_TRUE(latin.has_value());
  ASSERT_TRUE(ascii.has_value());
  for (auto &byte : f.bytes)
    byte = 0;
  EXPECT_STREQ(*utf8, "caf\xc3\xa9");
  EXPECT_STREQ(*latin, "caf\xe9");
  EXPECT_STREQ(*ascii, "caf?");
  ::free(*utf8);
  ::free(*latin);
  ::free(*ascii);
}

TEST(LlvmLibcMessageTranslationTest, ConversionGrowth) {
  Fixture f;
  auto view = f.view();
  ASSERT_TRUE(view.has_value());
  auto result = translate_message(*view, "long", cpp::nullopt, "UTF-32BE");
  ASSERT_TRUE(result.has_value());
  for (unsigned i = 0; i != 100; ++i) {
    EXPECT_EQ((*result)[4 * i], char(0));
    EXPECT_EQ((*result)[4 * i + 1], char(0));
    EXPECT_EQ((*result)[4 * i + 2], char(0));
    EXPECT_EQ(static_cast<unsigned char>((*result)[4 * i + 3]),
              static_cast<unsigned char>(0xe9));
  }
  EXPECT_EQ((*result)[400], char(0));
  ::free(*result);
}

TEST(LlvmLibcMessageTranslationTest, NoCharsetPreservesBytes) {
  Fixture f(false, "Plural-Forms: nplurals=3; plural=(n==1?0:n==2?1:2);\n");
  auto view = f.view();
  ASSERT_TRUE(view.has_value());
  auto result = translate_message(*view, "coffee", cpp::nullopt, "ASCII");
  ASSERT_TRUE(result.has_value());
  EXPECT_STREQ(*result, "caf\xe9");
  ::free(*result);
}

TEST(LlvmLibcMessageTranslationTest, MalformedMetadataAndEncoding) {
  const char *headers[] = {"Plural-Forms: nplurals=3; plural=1/0;\n",
                           "Plural-Forms: nplurals=3; plural=3;\n",
                           "Plural-Forms: nplurals=2; plural=n!=1;\n",
                           "Plural-Forms: invalid\n", ""};
  for (const char *header : headers) {
    Fixture f(false, header);
    auto view = f.view();
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(translate_message(*view, "apple", 2UL, "UTF-8").has_value());
  }
  Fixture invalid(false, "Content-Type: text/plain; charset=UTF-8\n");
  auto view = invalid.view();
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(translate_message(*view, "coffee", cpp::nullopt, "ASCII").error(),
            EINVAL);
  Fixture normal;
  view = normal.view();
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(
      translate_message(*view, "coffee", cpp::nullopt, "unsupported").error(),
      EINVAL);
}

#ifdef MESSAGE_TRANSLATION_ALLOCATION_TEST
namespace {
bool tracking = false;
unsigned calls = 0, fail_call = 0, live = 0;
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *, size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++calls == fail_call)
    return nullptr;
  void *p = __real_malloc(size);
  if (tracking && p)
    ++live;
  return p;
}
extern "C" void *__wrap_realloc(void *old, size_t size) {
  if (tracking && ++calls == fail_call)
    return nullptr;
  void *p = __real_realloc(old, size);
  if (tracking && p && !old)
    ++live;
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (tracking && p)
    --live;
  __real_free(p);
}

TEST(LlvmLibcMessageTranslationTest, AllocationAndGrowthRollback) {
  Fixture f;
  auto view = f.view();
  ASSERT_TRUE(view.has_value());
  calls = live = fail_call = 0;
  tracking = true;
  auto baseline = translate_message(*view, "long", cpp::nullopt, "UTF-32BE");
  if (baseline)
    ::free(*baseline);
  unsigned total = calls;
  tracking = false;
  ASSERT_TRUE(baseline.has_value());
  EXPECT_EQ(live, 0U);
  EXPECT_GT(total, 2U);
  for (unsigned failure = 1; failure <= total; ++failure) {
    calls = live = 0;
    fail_call = failure;
    tracking = true;
    auto result = translate_message(*view, "long", cpp::nullopt, "UTF-32BE");
    if (result)
      ::free(*result);
    tracking = false;
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ENOMEM);
    EXPECT_EQ(live, 0U);
  }
  Fixture malformed(false, "Content-Type: text/plain; charset=UTF-8\n");
  view = malformed.view();
  ASSERT_TRUE(view.has_value());
  calls = live = fail_call = 0;
  tracking = true;
  auto result = translate_message(*view, "coffee", cpp::nullopt, "ASCII");
  if (result)
    ::free(*result);
  tracking = false;
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(live, 0U);
}
#endif
