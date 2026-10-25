//===-- Embedded transliteration tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/builtin_transliteration.h"
#include "src/__support/wchar/encoding_converter.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::cpp::span;

TEST(LlvmLibcBuiltinTransliterationTest, ValidateCompleteImmutableTable) {
  auto data = builtin_transliteration();
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(data->size(), size_t(6527));
  auto again = builtin_transliteration();
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(data->rule(0)->words.bytes.data(),
            again->rule(0)->words.bytes.data());
  const uint32_t umlaut[] = {0xe4}, combining[] = {0x308};
  auto rule = data->find(umlaut);
  ASSERT_TRUE(rule.has_value());
  EXPECT_EQ(rule->alternative(0)->word(0).value(), uint32_t('a'));
  auto deletion = data->find(combining);
  ASSERT_TRUE(deletion.has_value());
  EXPECT_TRUE(deletion->alternative(0)->bytes.empty());
}

TEST(LlvmLibcBuiltinTransliterationTest, RealRulesDeletionAndFallback) {
  auto rules = builtin_transliteration();
  ASSERT_TRUE(rules.has_value());
  auto converter = EncodingConverter::from_names("UTF-8", "ASCII//TRANSLIT");
  ASSERT_TRUE(converter.has_value());
  // a-umlaut, combining diaeresis, copyright, unmapped private-use scalar.
  const uint8_t input[] = {0xc3, 0xa4, 0xcc, 0x88, 0xc2, 0xa9, 0xee, 0x80, 0x80};
  uint8_t output[16]{};
  span<const uint8_t> in(input);
  span<uint8_t> out(output);
  auto result = converter->convert(in, out, &*rules);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, size_t(4));
  EXPECT_TRUE(in.empty());
  EXPECT_EQ(out.data() - output, ptrdiff_t(5));
  EXPECT_STREQ(reinterpret_cast<const char *>(output), "a(C)?");
}
