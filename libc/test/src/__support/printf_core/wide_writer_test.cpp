//===-- Unittests for bounded wide printf output
//---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/limits.h"
#include "src/__support/printf_core/wide_writer.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::printf_core;

namespace {

struct GuardedBuffer {
  wchar_t before = L'B';
  wchar_t data[4] = {L'x', L'x', L'x', L'x'};
  wchar_t after = L'A';

  void check_guards() const {
    EXPECT_EQ(before, L'B');
    EXPECT_EQ(after, L'A');
  }
};

} // namespace

TEST(LlvmLibcWidePrintfWriterTest, EmptyOutput) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  EXPECT_EQ(writer.write({L"", 0}), WRITE_OK);
  EXPECT_EQ(writer.write(L'x', 0), WRITE_OK);
  EXPECT_EQ(writer.write_ascii({"", 0}), WRITE_OK);
  EXPECT_EQ(writer.get_chars_written(), size_t(0));
  EXPECT_EQ(writer.get_error(), WRITE_OK);
  EXPECT_EQ(buffer.data[0], L'\0');
  EXPECT_EQ(buffer.data[1], L'x');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, ZeroCapacity) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 0);
  EXPECT_EQ(writer.get_error(), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.write(L'a'), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.write({L"", 0}), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.write_ascii("12"), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.get_chars_written(), size_t(0));
  for (wchar_t value : buffer.data)
    EXPECT_EQ(value, L'x');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, NullBufferWithZeroCapacity) {
  WideWriter writer(nullptr, 0);
  EXPECT_EQ(writer.write(L'a'), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.get_chars_written(), size_t(0));
}

TEST(LlvmLibcWidePrintfWriterTest, TerminatorOnly) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 1);
  EXPECT_EQ(writer.write({L"", 0}), WRITE_OK);
  EXPECT_EQ(writer.write(L'\u03bb'), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.get_chars_written(), size_t(0));
  EXPECT_EQ(buffer.data[0], L'\0');
  for (size_t i = 1; i < 4; ++i)
    EXPECT_EQ(buffer.data[i], L'x');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, WideSpanExactFit) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  EXPECT_EQ(writer.write(L"\u03bb\u4e2d\u00e9"), WRITE_OK);
  EXPECT_EQ(writer.get_chars_written(), size_t(3));
  EXPECT_EQ(buffer.data[0], L'\u03bb');
  EXPECT_EQ(buffer.data[1], L'\u4e2d');
  EXPECT_EQ(buffer.data[2], L'\u00e9');
  EXPECT_EQ(buffer.data[3], L'\0');
  EXPECT_EQ(writer.write({L"", 0}), WRITE_OK);
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, WideSpanTruncation) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  EXPECT_EQ(writer.write(L"\u03bb\u4e2d\u00e9x"), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.get_chars_written(), size_t(3));
  EXPECT_EQ(buffer.data[0], L'\u03bb');
  EXPECT_EQ(buffer.data[1], L'\u4e2d');
  EXPECT_EQ(buffer.data[2], L'\u00e9');
  EXPECT_EQ(buffer.data[3], L'\0');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, EmbeddedNull) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  const wchar_t text[] = {L'\0', L'\u4e2d'};
  EXPECT_EQ(writer.write({text, 2}), WRITE_OK);
  EXPECT_EQ(writer.write(L'\0'), WRITE_OK);
  EXPECT_EQ(writer.get_chars_written(), size_t(3));
  EXPECT_EQ(buffer.data[0], L'\0');
  EXPECT_EQ(buffer.data[1], L'\u4e2d');
  EXPECT_EQ(buffer.data[2], L'\0');
  EXPECT_EQ(buffer.data[3], L'\0');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, PaddingUsesWideElements) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  EXPECT_EQ(writer.write(L'\u4e2d', 3), WRITE_OK);
  EXPECT_EQ(writer.get_chars_written(), size_t(3));
  for (size_t i = 0; i < 3; ++i)
    EXPECT_EQ(buffer.data[i], L'\u4e2d');
  EXPECT_EQ(buffer.data[3], L'\0');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, PaddingTruncation) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  EXPECT_EQ(writer.write(L'a'), WRITE_OK);
  EXPECT_EQ(writer.write(L'\u03bb', 100), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.get_chars_written(), size_t(3));
  EXPECT_EQ(buffer.data[0], L'a');
  EXPECT_EQ(buffer.data[1], L'\u03bb');
  EXPECT_EQ(buffer.data[2], L'\u03bb');
  EXPECT_EQ(buffer.data[3], L'\0');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, RepeatedPadding) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  EXPECT_EQ(writer.write(L' ', 2), WRITE_OK);
  EXPECT_EQ(writer.write(L'\u03bb', 1), WRITE_OK);
  EXPECT_EQ(writer.write(L' ', 0), WRITE_OK);
  EXPECT_EQ(writer.get_chars_written(), size_t(3));
  EXPECT_EQ(buffer.data[0], L' ');
  EXPECT_EQ(buffer.data[1], L' ');
  EXPECT_EQ(buffer.data[2], L'\u03bb');
  EXPECT_EQ(buffer.data[3], L'\0');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, IntMaxPaddingIsBoundedByCapacity) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  EXPECT_EQ(writer.write(L' ', LIBC_NAMESPACE::cpp::numeric_limits<int>::max()),
            BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.get_chars_written(), size_t(3));
  for (size_t i = 0; i < 3; ++i)
    EXPECT_EQ(buffer.data[i], L' ');
  EXPECT_EQ(buffer.data[3], L'\0');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, MixedWrites) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  EXPECT_EQ(writer.write(L'\u4e2d'), WRITE_OK);
  EXPECT_EQ(writer.write_ascii("12"), WRITE_OK);
  EXPECT_EQ(writer.get_chars_written(), size_t(3));
  EXPECT_EQ(buffer.data[0], L'\u4e2d');
  EXPECT_EQ(buffer.data[1], L'1');
  EXPECT_EQ(buffer.data[2], L'2');
  EXPECT_EQ(buffer.data[3], L'\0');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, ASCIITruncation) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  EXPECT_EQ(writer.write_ascii("1234"), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.get_chars_written(), size_t(3));
  EXPECT_EQ(buffer.data[0], L'1');
  EXPECT_EQ(buffer.data[1], L'2');
  EXPECT_EQ(buffer.data[2], L'3');
  EXPECT_EQ(buffer.data[3], L'\0');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, StickyFailure) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 2);
  EXPECT_EQ(writer.write(L"ab"), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.write(L'x', 3), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.write_ascii("12"), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.write(L"xyz"), BUFFER_TOO_SMALL);
  EXPECT_EQ(writer.get_chars_written(), size_t(1));
  EXPECT_EQ(buffer.data[0], L'a');
  EXPECT_EQ(buffer.data[1], L'\0');
  EXPECT_EQ(buffer.data[2], L'x');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, CountOverflow) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  EXPECT_EQ(writer.write(L'a'), WRITE_OK);
  EXPECT_EQ(writer.write(L' ', LIBC_NAMESPACE::cpp::numeric_limits<int>::max()),
            OVERFLOW_ERROR);
  EXPECT_EQ(writer.get_chars_written(), size_t(1));
  EXPECT_EQ(writer.write(L'b'), OVERFLOW_ERROR);
  EXPECT_EQ(buffer.data[0], L'a');
  EXPECT_EQ(buffer.data[1], L'\0');
  EXPECT_EQ(buffer.data[2], L'x');
  buffer.check_guards();
}

TEST(LlvmLibcWidePrintfWriterTest, MaximumPaddingDoesNotWrap) {
  GuardedBuffer buffer;
  WideWriter writer(buffer.data, 4);
  EXPECT_EQ(
      writer.write(L' ', LIBC_NAMESPACE::cpp::numeric_limits<size_t>::max()),
      OVERFLOW_ERROR);
  EXPECT_EQ(writer.get_chars_written(), size_t(0));
  EXPECT_EQ(buffer.data[0], L'\0');
  EXPECT_EQ(buffer.data[1], L'x');
  buffer.check_guards();
}
