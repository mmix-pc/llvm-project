//===-- Encoded dollar quote tests ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/wordexp/quote.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::CharVector;
using LIBC_NAMESPACE::cpp::string_view;

TEST(LlvmLibcWordQuoteTest, SourceCharactersAndRawBytes) {
  WordSource source;
  ASSERT_EQ(source.assign("a\xc4\xb1\\n\\xFF\\0discarded",
                          RestartableConverter(true)),
            0);
  CharVector output;
  ASSERT_TRUE(bool(render_word_quote(source, 0, source.scalars().size(),
                                     RestartableConverter(true), output)));
  EXPECT_TRUE(string_view(output.c_str(), output.length()) ==
              "a\xc4\xb1\n\xff");
}

TEST(LlvmLibcWordQuoteTest, EncodingAndPreflightErrorsAreDistinct) {
  const uint32_t input[] = {0x4e2d};
  CharVector output;
  auto result = render_word_quote(input, RestartableConverter(false), output);
  EXPECT_TRUE(result.error == WordSyntaxError::SINK_ERROR);
  EXPECT_EQ(result.sink_error, EILSEQ);
  EXPECT_EQ(output.length(), size_t(0));
  WordSource source;
  ASSERT_EQ(source.assign("text\\q", RestartableConverter(true)), 0);
  result = render_word_quote(source, 0, source.scalars().size(),
                             RestartableConverter(true), output);
  EXPECT_TRUE(result.error == WordSyntaxError::SYNTAX);
  EXPECT_EQ(output.length(), size_t(0));
}

TEST(LlvmLibcWordQuoteTest, RejectsInvalidDestinationAndSourceRanges) {
  WordSource source;
  ASSERT_EQ(source.assign("value", RestartableConverter(true)), 0);
  CharVector output;
  ASSERT_TRUE(output.append('x'));
  auto result =
      render_word_quote(source, 0, 5, RestartableConverter(true), output);
  EXPECT_EQ(result.sink_error, EINVAL);
  EXPECT_STREQ(output.c_str(), "x");
  CharVector empty;
  result = render_word_quote(source, 2, 1, RestartableConverter(true), empty);
  EXPECT_EQ(result.sink_error, EINVAL);
  result = render_word_quote(source, 0, 6, RestartableConverter(true), empty);
  EXPECT_EQ(result.sink_error, EINVAL);
  EXPECT_EQ(empty.length(), size_t(0));
}
