//===-- Stateful diagnostic batching tests --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/File/file.h"
#include "src/__support/wchar/iso2022_cn.h"
#include "src/locale/locale.h"
#include "test/UnitTest/Test.h"
#include "test/src/__support/wchar/ISO2022CNFixture.h"

using LIBC_NAMESPACE::File;
using LIBC_NAMESPACE::FileIOResult;
using LIBC_NAMESPACE::internal::ChineseEncodingData;
using LIBC_NAMESPACE::internal::ISO2022CNState;

namespace {
struct SelectedEncoding {
  __locale_data category;
  __locale_t locale;
  locale_t previous;
  explicit SelectedEncoding(const ChineseEncodingData &data)
      : category("diagnostic-test", false),
        locale(LIBC_NAMESPACE::internal::locale_from_mask(0)),
        previous(LIBC_NAMESPACE::internal::thread_locale) {
    category.chinese_encoding = data;
    locale.__data[LC_CTYPE] = &category;
    LIBC_NAMESPACE::internal::thread_locale = &locale;
  }
  ~SelectedEncoding() { LIBC_NAMESPACE::internal::thread_locale = previous; }
};

class StateStream : public File {
  uint8_t buffer[32]{};
  static FileIOResult output(File *base, const void *in, size_t count) {
    auto &f = *static_cast<StateStream *>(base);
    ++f.calls;
    size_t n = 0;
    while (n < count && f.offset < f.stop && f.offset < sizeof(f.bytes))
      f.bytes[f.offset++] = static_cast<const uint8_t *>(in)[n++];
    return {n, f.error_with_progress || n != count ? EINTR : 0};
  }
  static LIBC_NAMESPACE::ErrorOr<off_t> position(File *base, off_t at,
                                                 int how) {
    EXPECT_EQ(at, off_t(0));
    EXPECT_EQ(how, SEEK_CUR);
    return off_t(static_cast<StateStream *>(base)->offset);
  }
  static int finish(File *) { return 0; }
  static int synchronize(File *base) {
    auto &f = *static_cast<StateStream *>(base);
    ++f.flush_calls;
    return f.flush_error;
  }

public:
  uint8_t bytes[512]{};
  size_t offset = 0, stop = sizeof(bytes);
  unsigned calls = 0, flush_calls = 0;
  bool error_with_progress = false;
  int flush_error = 0;
  explicit StateStream(int mode = _IONBF, bool flush_hook = false)
      : File(output, nullptr, position, finish, buffer, sizeof(buffer), mode,
             false, File::mode_flags("w"), flush_hook ? synchronize : nullptr) {
  }
  ~StateStream() { close(); }
};

class NativeWideSink : public File {
  static FileIOResult output(File *base, const wchar_t *in, size_t count) {
    auto &sink = *static_cast<NativeWideSink *>(base);
    ++sink.calls;
    EXPECT_EQ(count, size_t(1));
    sink.last = in[0];
    return {1, EIO};
  }

public:
  unsigned calls = 0;
  wchar_t last = 0;
  NativeWideSink()
      : File(nullptr, nullptr, nullptr, nullptr, nullptr, 0, _IONBF, false,
             File::mode_flags("w"), nullptr, false, output) {}
};
} // namespace

TEST(LlvmLibcDiagnosticStateTest, CommitsOnlyCompletedCharacters) {
  LIBC_NAMESPACE::testing::ISO2022CNFixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  SelectedEncoding locale(*data);
  const char wire[] = "\x1b$)A\x0e!!!\"\x0f"
                      "A";
  for (size_t stop = 0; stop <= sizeof(wire) - 1; ++stop) {
    StateStream stream;
    ASSERT_EQ(stream.try_set_orientation_unlocked(File::Orientation::WIDE),
              File::Orientation::WIDE);
    stream.stop = stop;
    auto result = stream.write_diagnostic_unlocked(wire, sizeof(wire) - 1);
    size_t consumed = stop >= 11 ? 11 : stop >= 9 ? 9 : stop >= 7 ? 7 : 0;
    EXPECT_EQ(result.value, consumed);
    EXPECT_EQ(result.error, stop == sizeof(wire) - 1 ? 0 : EINTR);
    EXPECT_EQ(stream.offset, stop);
    for (size_t i = 0; i < stop; ++i)
      EXPECT_EQ(stream.bytes[i], uint8_t(wire[i]));
    auto position = stream.get_position();
    ASSERT_TRUE(position.has_value());
    auto saved = ISO2022CNState::load(position->state);
    ASSERT_TRUE(saved.has_value());
    // Returning to ASCII does not discard the established designation.
    EXPECT_EQ(saved->initial(), consumed == 0);
    stream.stop = sizeof(stream.bytes);
    stream.clearerr();
    ASSERT_EQ(stream.flush(), 0);
    EXPECT_EQ(stream.offset, stop);
    ASSERT_EQ(stream.write(L"B", 1).value, size_t(1));
    const bool shifted = consumed == 7 || consumed == 9;
    EXPECT_EQ(stream.offset, stop + (shifted ? 2 : 1));
    if (shifted)
      EXPECT_EQ(stream.bytes[stop], uint8_t(0x0f));
    EXPECT_EQ(stream.bytes[stream.offset - 1], uint8_t('B'));
  }
}

TEST(LlvmLibcDiagnosticStateTest, CustomFlushCommitsBeforeReportingError) {
  LIBC_NAMESPACE::testing::ISO2022CNFixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  SelectedEncoding locale(*data);
  const char wire[] = "\x1b$)A\x0e!!!\"";
  const bool backend_errors[] = {false, true};
  for (bool backend_error : backend_errors) {
    StateStream stream(_IONBF, true);
    ASSERT_EQ(stream.try_set_orientation_unlocked(File::Orientation::WIDE),
              File::Orientation::WIDE);
    stream.error_with_progress = backend_error;
    stream.flush_error = backend_error ? 0 : EIO;
    auto result = stream.write_diagnostic_unlocked(wire, sizeof(wire) - 1);
    EXPECT_EQ(result.value, size_t(7));
    EXPECT_EQ(result.error, backend_error ? EINTR : EIO);
    EXPECT_EQ(stream.offset, size_t(7));
    EXPECT_EQ(stream.calls, 1U);
    EXPECT_EQ(stream.flush_calls, backend_error ? 0U : 1U);
    auto pos = stream.get_position();
    ASSERT_TRUE(pos.has_value());
    auto saved = ISO2022CNState::load(pos->state);
    ASSERT_TRUE(saved.has_value());
    EXPECT_FALSE(saved->initial());
    stream.error_with_progress = false;
    stream.flush_error = 0;
    stream.clearerr();
    ASSERT_EQ(stream.write_diagnostic_unlocked("B", 1).value, size_t(1));
    EXPECT_EQ(stream.offset, size_t(9));
    EXPECT_EQ(stream.bytes[7], uint8_t(0x0f));
    EXPECT_EQ(stream.bytes[8], uint8_t('B'));
  }
}

TEST(LlvmLibcDiagnosticStateTest, CustomFlushRetainsPerCharacterCadence) {
  LIBC_NAMESPACE::testing::ISO2022CNFixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  SelectedEncoding locale(*data);
  StateStream stream(_IONBF, true);
  ASSERT_EQ(stream.try_set_orientation_unlocked(File::Orientation::WIDE),
            File::Orientation::WIDE);
  const char wire[] = "\x1b$)A\x0e!!!\"";
  auto result = stream.write_diagnostic_unlocked(wire, sizeof(wire) - 1);
  EXPECT_EQ(result.value, sizeof(wire) - 1);
  EXPECT_EQ(result.error, 0);
  EXPECT_EQ(stream.offset, sizeof(wire) - 1);
  EXPECT_EQ(stream.calls, 2U);
  EXPECT_EQ(stream.flush_calls, 2U);
}

TEST(LlvmLibcDiagnosticStateTest, OrdinaryWideErrorContractIsUnchanged) {
  LIBC_NAMESPACE::testing::ISO2022CNFixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  SelectedEncoding locale(*data);
  StateStream stream(_IONBF, true);
  stream.error_with_progress = true;
  auto result = stream.write(L"\u4e2d", 1);
  EXPECT_EQ(result.value, size_t(0));
  EXPECT_EQ(result.error, EINTR);
  EXPECT_EQ(stream.offset, size_t(7));
  auto position = stream.get_position();
  ASSERT_TRUE(position.has_value());
  auto saved = ISO2022CNState::load(position->state);
  ASSERT_TRUE(saved.has_value());
  EXPECT_TRUE(saved->initial());
}

TEST(LlvmLibcDiagnosticStateTest, LineBufferedCompletedErrorIsDiagnosticOnly) {
  const bool diagnostics[] = {false, true};
  for (bool diagnostic : diagnostics) {
    StateStream stream(_IOLBF);
    ASSERT_EQ(stream.try_set_orientation_unlocked(File::Orientation::WIDE),
              File::Orientation::WIDE);
    stream.error_with_progress = true;
    auto result = diagnostic ? stream.write_diagnostic_unlocked("\n", 1)
                             : stream.write(L"\n", 1);
    EXPECT_EQ(result.value, diagnostic ? size_t(1) : size_t(0));
    EXPECT_EQ(result.error, EINTR);
    EXPECT_EQ(stream.offset, size_t(1));
    EXPECT_EQ(stream.bytes[0], uint8_t('\n'));
    EXPECT_EQ(stream.calls, 1U);
    stream.clearerr();
    EXPECT_EQ(stream.flush(), 0);
    EXPECT_EQ(stream.calls, 1U);
  }
}

TEST(LlvmLibcDiagnosticStateTest, LineBufferedPartialAndOlderFlushFailures) {
  StateStream partial(_IOLBF);
  ASSERT_EQ(partial.try_set_orientation_unlocked(File::Orientation::WIDE),
            File::Orientation::WIDE);
  partial.stop = 1;
  auto result = partial.write_diagnostic_unlocked("A\n", 2);
  EXPECT_EQ(result.value, size_t(1));
  EXPECT_EQ(result.error, EINTR);
  EXPECT_EQ(partial.offset, size_t(1));
  partial.stop = sizeof(partial.bytes);
  partial.clearerr();
  EXPECT_EQ(partial.flush(), 0);
  EXPECT_EQ(partial.offset, size_t(2));
  EXPECT_EQ(partial.bytes[0], uint8_t('A'));
  EXPECT_EQ(partial.bytes[1], uint8_t('\n'));

  StateStream older(_IOLBF);
  ASSERT_EQ(older.try_set_orientation_unlocked(File::Orientation::WIDE),
            File::Orientation::WIDE);
  older.error_with_progress = true;
  // Eight wchar_t values fill the buffer; failure flushing them must not
  // count the newline which has not yet been accepted.
  result = older.write_diagnostic_unlocked("abcdefgh\n", 9);
  EXPECT_EQ(result.value, size_t(8));
  EXPECT_EQ(result.error, EINTR);
  EXPECT_EQ(older.offset, size_t(8));
  EXPECT_EQ(older.calls, 1U);
}

TEST(LlvmLibcDiagnosticStateTest, NativeWideProgressCountsSourceBytes) {
  LIBC_NAMESPACE::testing::ISO2022CNFixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  SelectedEncoding locale(*data);
  NativeWideSink stream;
  ASSERT_EQ(stream.try_set_orientation_unlocked(File::Orientation::WIDE),
            File::Orientation::WIDE);
  const char wire[] = "\x1b$)A\x0e!!!\"";
  auto result = stream.write_diagnostic_unlocked(wire, sizeof(wire) - 1);
  EXPECT_EQ(result.value, size_t(7));
  EXPECT_EQ(result.error, EIO);
  EXPECT_EQ(stream.calls, 1U);
  EXPECT_EQ(stream.last, wchar_t(0x4e2d));
  EXPECT_TRUE(stream.error());
}

TEST(LlvmLibcDiagnosticStateTest, StatefulProgressAcrossMultipleBatches) {
  LIBC_NAMESPACE::testing::ISO2022CNFixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  SelectedEncoding locale(*data);
  char wire[207] = {'\x1b', '$', ')', 'A', '\x0e'};
  for (size_t i = 5; i < 205; ++i)
    wire[i] = '!';
  wire[205] = '\x0f';
  wire[206] = 'A';
  const size_t stops[] = {0, 6, 7, 130, 131, 132, 133, 134, 205, 206, 207};
  for (size_t stop : stops) {
    StateStream stream;
    ASSERT_EQ(stream.try_set_orientation_unlocked(File::Orientation::WIDE),
              File::Orientation::WIDE);
    stream.stop = stop;
    auto result = stream.write_diagnostic_unlocked(wire, sizeof(wire));
    size_t consumed = stop < 7      ? 0
                      : stop >= 207 ? 207
                      : stop >= 205 ? 205
                                    : 5 + 2 * ((stop - 5) / 2);
    EXPECT_EQ(result.value, consumed);
    EXPECT_EQ(result.error, stop == sizeof(wire) ? 0 : EINTR);
    EXPECT_EQ(stream.offset, stop);
    for (size_t i = 0; i < stop; ++i)
      EXPECT_EQ(stream.bytes[i], uint8_t(wire[i]));
    stream.stop = sizeof(stream.bytes);
    stream.clearerr();
    EXPECT_EQ(stream.flush(), 0);
    EXPECT_EQ(stream.offset, stop);
    ASSERT_EQ(stream.write_diagnostic_unlocked("B", 1).value, size_t(1));
    const bool shifted = consumed && consumed < sizeof(wire);
    EXPECT_EQ(stream.offset, stop + (shifted ? 2 : 1));
    if (shifted)
      EXPECT_EQ(stream.bytes[stop], uint8_t(0x0f));
    EXPECT_EQ(stream.bytes[stream.offset - 1], uint8_t('B'));
  }
}
