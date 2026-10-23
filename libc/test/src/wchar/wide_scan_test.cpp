//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/stdio_macros.h"
#include "hdr/wchar_macros.h"
#include "src/stdio/fclose.h"
#include "src/stdio/feof.h"
#include "src/stdio/ferror.h"
#include "src/stdio/fflush.h"
#include "src/stdio/fgetpos.h"
#include "src/stdio/fopen.h"
#include "src/stdio/fseek.h"
#include "src/stdio/fsetpos.h"
#include "src/stdio/ftell.h"
#include "src/stdio/fwrite.h"
#include "src/stdio/setvbuf.h"
#include "src/stdio/stdin.h"
#include "src/wchar/fgetwc.h"
#include "src/wchar/fwide.h"
#include "src/wchar/fwscanf.h"
#include "src/wchar/swscanf.h"
#include "src/wchar/ungetwc.h"
#include "src/wchar/vfwscanf.h"
#include "src/wchar/vswscanf.h"
#include "src/wchar/vwscanf.h"
#include "src/wchar/wscanf.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/src/locale/LocaleGuard.h"
#include <stdarg.h>

static int scan(const wchar_t *buffer, const wchar_t *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = LIBC_NAMESPACE::vswscanf(buffer, fmt, args);
  va_end(args);
  return result;
}

static int stream_scan(::FILE *file, const wchar_t *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = LIBC_NAMESPACE::vfwscanf(file, fmt, args);
  va_end(args);
  return result;
}

static int stdin_scan(const wchar_t *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = LIBC_NAMESPACE::vwscanf(fmt, args);
  va_end(args);
  return result;
}

static ::FILE *input_file(const char *bytes, size_t size) {
  auto name = libc_make_test_file_path("wide-scan-input");
  ::FILE *file = LIBC_NAMESPACE::fopen(name, "w");
  if (!file)
    return nullptr;
  size_t written = LIBC_NAMESPACE::fwrite(bytes, 1, size, file);
  int closed = LIBC_NAMESPACE::fclose(file);
  if (written != size || closed != 0)
    return nullptr;
  return LIBC_NAMESPACE::fopen(name, "r");
}

TEST(LlvmLibcWideScanTest, IntegerWidthsAndSuppression) {
  int i = -1, n = -1;
  unsigned int x = 0, oct = 0;
  EXPECT_EQ(scan(L" 42 0xff 077", L" %d %x %o%n", &i, &x, &oct, &n), 3);
  EXPECT_EQ(i, 42);
  EXPECT_EQ(x, 255U);
  EXPECT_EQ(oct, 63U);
  EXPECT_EQ(n, 12);
  EXPECT_EQ(scan(L"12345", L"%*2d%3d%n", &i, &n), 1);
  EXPECT_EQ(i, 345);
  EXPECT_EQ(n, 5);
  EXPECT_EQ(scan(L"12", L"%*d"), 0);
  EXPECT_EQ(scan(L"\u0131", L"%d", &i), 0);
  EXPECT_EQ(i, 345);
}

#ifndef LIBC_COPT_SCANF_DISABLE_INDEX_MODE
TEST(LlvmLibcWideScanTest, NumberedArguments) {
  int first = -1, second = -1;
  EXPECT_EQ(LIBC_NAMESPACE::swscanf(L"11 22", L"%2$d %1$d", &first, &second),
            2);
  EXPECT_EQ(first, 22);
  EXPECT_EQ(second, 11);
  EXPECT_EQ(scan(L"11 99 % 22", L"%2$d %*d %% %1$d", &first, &second), 2);
  EXPECT_EQ(first, 22);
  EXPECT_EQ(second, 11);
  ::FILE *file = input_file("11 22!", 6);
  ASSERT_NE(file, nullptr);
  EXPECT_EQ(stream_scan(file, L"%2$d %1$d", &first, &second), 2);
  EXPECT_EQ(first, 22);
  EXPECT_EQ(second, 11);
  EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L'!'));
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}
#endif

TEST(LlvmLibcWideScanTest, WideAliases) {
  wchar_t ch = 0, str[3] = {};
  EXPECT_EQ(scan(L"A BC", L"%C %S", &ch, str), 2);
  EXPECT_EQ(ch, L'A');
  EXPECT_EQ(str[0], L'B');
  EXPECT_EQ(str[1], L'C');
  EXPECT_EQ(str[2], wchar_t(0));
}

TEST(LlvmLibcWideScanTest, AllocatingStringsAndCharacters) {
  LocaleGuard locale;
  char *bytes = nullptr;
  wchar_t *wide = nullptr;
  EXPECT_EQ(scan(L"\u00a2 ab", L"%ms %mls", &bytes, &wide), 2);
  ASSERT_NE(bytes, nullptr);
  ASSERT_NE(wide, nullptr);
  EXPECT_STREQ(bytes, "\xc2\xa2");
  EXPECT_EQ(wide[0], L'a');
  EXPECT_EQ(wide[1], L'b');
  EXPECT_EQ(wide[2], wchar_t(0));
  ::free(bytes);
  ::free(wide);
  EXPECT_EQ(scan(L"aba!", L"%m[ab]", &bytes), 1);
  EXPECT_STREQ(bytes, "aba");
  ::free(bytes);
  EXPECT_EQ(scan(L"aba!", L"%ml[ab]", &wide), 1);
  EXPECT_EQ(wide[0], L'a');
  EXPECT_EQ(wide[1], L'b');
  EXPECT_EQ(wide[2], L'a');
  EXPECT_EQ(wide[3], wchar_t(0));
  ::free(wide);
  EXPECT_EQ(scan(L"\u00a2", L"%mc", &bytes), 1);
  EXPECT_EQ(int(static_cast<unsigned char>(bytes[0])), 0xc2);
  EXPECT_EQ(int(static_cast<unsigned char>(bytes[1])), 0xa2);
  ::free(bytes);
  EXPECT_EQ(scan(L"AB", L"%2mlc", &wide), 1);
  EXPECT_EQ(wide[0], L'A');
  EXPECT_EQ(wide[1], L'B');
  ::free(wide);
}

TEST(LlvmLibcWideScanTest, IntegerLengthModifiers) {
  signed char hh = 0;
  short h = 0;
  long l = 0;
  long long ll = 0;
  intmax_t j = 0;
  size_t z = 0;
  ptrdiff_t t = 0;
  EXPECT_EQ(scan(L"-1 -2 -3 -4 -5 6 -7", L"%hhd %hd %ld %lld %jd %zu %td", &hh,
                 &h, &l, &ll, &j, &z, &t),
            7);
  EXPECT_EQ(hh, static_cast<signed char>(-1));
  EXPECT_EQ(h, short(-2));
  EXPECT_EQ(l, -3L);
  EXPECT_EQ(ll, -4LL);
  EXPECT_EQ(j, intmax_t(-5));
  EXPECT_EQ(z, size_t(6));
  EXPECT_EQ(t, ptrdiff_t(-7));
}

TEST(LlvmLibcWideScanTest, IncompleteNumbersDoNotAssign) {
  int i = 99, n = -1;
  double d = 99;
  EXPECT_EQ(scan(L"0x!", L"%i%n", &i, &n), 0);
  EXPECT_EQ(i, 99);
  EXPECT_EQ(n, -1);
  EXPECT_EQ(scan(L"-", L"%d", &i), 0);
  EXPECT_EQ(i, 99);
  EXPECT_EQ(scan(L"0x1p ", L"%lf%n", &d, &n), 0);
  EXPECT_TRUE(d == 99);
  EXPECT_EQ(scan(L"100e+!", L"%lf", &d), 0);
  EXPECT_TRUE(d == 99);
  EXPECT_EQ(scan(L"0x", L"%lf", &d), 0);
}

TEST(LlvmLibcWideScanTest, FloatWidthsAndVaList) {
  float f = 0;
  double d = 0;
  long double ld = 0;
  wchar_t next = 0;
  EXPECT_EQ(scan(L"1.5 0x.1p4 -2.5e2!", L"%f %lf %Lf%lc", &f, &d, &ld, &next),
            4);
  EXPECT_TRUE(f == 1.5f);
  EXPECT_TRUE(d == 1.0);
  EXPECT_TRUE(ld == -250.0L);
  EXPECT_EQ(next, L'!');
  EXPECT_EQ(LIBC_NAMESPACE::swscanf(L"12.5x", L"%2f%lc", &f, &next), 2);
  EXPECT_TRUE(f == 12);
  EXPECT_EQ(next, L'.');
  EXPECT_EQ(scan(L"inf!", L"%lf%lc", &d, &next), 2);
  EXPECT_TRUE(__builtin_isinf(d) != 0);
  EXPECT_EQ(next, L'!');
  EXPECT_EQ(scan(L"nan(payload)!", L"%lf%lc", &d, &next), 2);
  EXPECT_TRUE(__builtin_isnan(d) != 0);
  EXPECT_EQ(next, L'!');
}

TEST(LlvmLibcWideScanTest, LongFloatingToken) {
  wchar_t text[180];
  text[0] = L'1';
  text[1] = L'.';
  for (int i = 2; i < 178; ++i)
    text[i] = L'0';
  text[178] = L'!';
  text[179] = 0;
  double value = 0;
  wchar_t next = 0;
  EXPECT_EQ(scan(text, L"%lf%lc", &value, &next), 2);
  EXPECT_TRUE(value == 1.0);
  EXPECT_EQ(next, L'!');
}

TEST(LlvmLibcWideScanTest, Pointers) {
  void *value = nullptr;
  EXPECT_EQ(scan(L"0x1234", L"%p", &value), 1);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(value), uintptr_t(0x1234));
  EXPECT_EQ(scan(L"(nullptr)", L"%p", &value), 1);
  EXPECT_EQ(value, nullptr);
  EXPECT_EQ(scan(L"(nullptr)", L"%*p"), 0);
}

TEST(LlvmLibcWideScanTest, WideLiteralsAndStrings) {
  LocaleGuard locale;
  wchar_t wide[8] = {};
  char bytes[16] = {};
  int n = -1;
  EXPECT_EQ(scan(L"\u20ac: \u20ac\u03b1 !", L"\u20ac: %2ls%n", wide, &n), 1);
  EXPECT_EQ(wide[0], L'\u20ac');
  EXPECT_EQ(wide[1], L'\u03b1');
  EXPECT_EQ(wide[2], L'\0');
  EXPECT_EQ(n, 5);
  EXPECT_EQ(scan(L"\u20ac\u03b1!", L"%2s%n", bytes, &n), 1);
  EXPECT_STREQ(bytes, "\xe2\x82\xac\xce\xb1");
  EXPECT_EQ(n, 2);
  EXPECT_EQ(scan(L"\u20ac", L"%lc", wide), 1);
  EXPECT_EQ(wide[0], L'\u20ac');
  EXPECT_EQ(scan(L"\u20ac", L"%c", bytes), 1);
  EXPECT_EQ(static_cast<unsigned char>(bytes[0]),
            static_cast<unsigned char>(0xe2));
  EXPECT_EQ(static_cast<unsigned char>(bytes[2]),
            static_cast<unsigned char>(0xac));
}

TEST(LlvmLibcWideScanTest, WideScanSets) {
  wchar_t wide[10] = {};
  char a[20] = {}, b[20] = {};
  int n = 0;
  EXPECT_EQ(scan(L"\u03b1\u03b2z", L"%l[\u03b1-\u03b3]%n", wide, &n), 1);
  EXPECT_EQ(wide[0], L'\u03b1');
  EXPECT_EQ(wide[1], L'\u03b2');
  EXPECT_EQ(wide[2], wchar_t(0));
  EXPECT_EQ(n, 2);
  EXPECT_EQ(scan(L"[abc123]....x", L"%10[^]]%10[].]%n", a, b, &n), 2);
  EXPECT_STREQ(a, "[abc123");
  EXPECT_STREQ(b, "]....");
  EXPECT_EQ(n, 12);
  wide[0] = L'!';
  EXPECT_EQ(scan(L"x", L"%l[\u03b1]", wide), 0);
  EXPECT_EQ(wide[0], L'!');
}

TEST(LlvmLibcWideScanTest, InputAndMatchingFailures) {
  int i = 9, n = -1;
  wchar_t c[3] = {};
  EXPECT_EQ(scan(L"", L"%d", &i), EOF);
  EXPECT_EQ(scan(L" ", L"%d", &i), EOF);
  EXPECT_EQ(scan(L"x", L"%d", &i), 0);
  EXPECT_EQ(scan(L"", L""), 0);
  EXPECT_EQ(scan(L"", L" "), 0);
  EXPECT_EQ(scan(L"", L"%n", &n), 0);
  EXPECT_EQ(n, 0);
  EXPECT_EQ(scan(L"a", L"%2lc", c), EOF);
  EXPECT_EQ(scan(L"1", L"%d%d", &i, &i), 1);
  EXPECT_EQ(scan(L"1", L"%*d%d", &i), 0);
  EXPECT_EQ(scan(L"%2", L"%%%d", &i), 1);
  EXPECT_EQ(i, 2);
}

TEST(LlvmLibcWideScanTest, StreamLookaheadAndCount) {
  LocaleGuard locale;
  ::FILE *file = input_file("\xe2\x82\xac 42!", 7);
  ASSERT_NE(file, nullptr);
  wchar_t c = 0;
  int i = 0, n = 0;
  EXPECT_EQ(stream_scan(file, L"%lc %d%n", &c, &i, &n), 2);
  EXPECT_EQ(c, L'\u20ac');
  EXPECT_EQ(i, 42);
  EXPECT_EQ(n, 4);
  EXPECT_EQ(LIBC_NAMESPACE::ftell(file), 6L);
  EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L'!'));
  EXPECT_EQ(LIBC_NAMESPACE::fwscanf(file, L"%d", &i), EOF);
  EXPECT_NE(LIBC_NAMESPACE::feof(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}

TEST(LlvmLibcWideScanTest, LookaheadAcrossBufferBoundary) {
  LocaleGuard locale;
  ::FILE *file = input_file("123\xe2\x82\xac!", 7);
  ASSERT_NE(file, nullptr);
  char buffer[4];
  ASSERT_EQ(LIBC_NAMESPACE::setvbuf(file, buffer, _IOFBF, sizeof(buffer)), 0);
  int value = 0;
  EXPECT_EQ(LIBC_NAMESPACE::fwscanf(file, L"%d", &value), 1);
  EXPECT_EQ(value, 123);
  EXPECT_EQ(LIBC_NAMESPACE::ftell(file), 3L);
  EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L'\u20ac'));
  EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L'!'));
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}

TEST(LlvmLibcWideScanTest, LookaheadPreservesPublicPushback) {
  LocaleGuard locale;
  ::FILE *file = input_file("123\xe2\x82\xac!", 7);
  ASSERT_NE(file, nullptr);
  char buffer[1];
  ASSERT_EQ(LIBC_NAMESPACE::setvbuf(file, buffer, _IOFBF, sizeof(buffer)), 0);
  int value = 0;
  EXPECT_EQ(LIBC_NAMESPACE::fwscanf(file, L"%d", &value), 1);
  EXPECT_EQ(value, 123);
  EXPECT_EQ(LIBC_NAMESPACE::ftell(file), 3L);
  EXPECT_EQ(LIBC_NAMESPACE::ungetwc(L'X', file), wint_t(L'X'));
  EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L'X'));
  EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L'\u20ac'));
  EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L'!'));
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}

TEST(LlvmLibcWideScanTest, LookaheadSynchronizesPosition) {
  LocaleGuard locale;
  for (int operation = 0; operation != 3; ++operation) {
    ::FILE *file = input_file("123\xe2\x82\xac!", 7);
    ASSERT_NE(file, nullptr);
    char buffer[1];
    ASSERT_EQ(LIBC_NAMESPACE::setvbuf(file, buffer, _IOFBF, sizeof(buffer)), 0);
    int value = 0;
    EXPECT_EQ(LIBC_NAMESPACE::fwscanf(file, L"%d", &value), 1);
    EXPECT_EQ(LIBC_NAMESPACE::ftell(file), 3L);
    fpos_t saved;
    EXPECT_EQ(LIBC_NAMESPACE::fgetpos(file, &saved), 0);
    if (operation == 0)
      EXPECT_EQ(LIBC_NAMESPACE::fflush(file), 0);
    else if (operation == 1)
      EXPECT_EQ(LIBC_NAMESPACE::fseek(file, 0, SEEK_CUR), 0);
    else {
      EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L'\u20ac'));
      EXPECT_EQ(LIBC_NAMESPACE::fsetpos(file, &saved), 0);
    }
    EXPECT_EQ(LIBC_NAMESPACE::ftell(file), 3L);
    EXPECT_EQ(LIBC_NAMESPACE::ungetwc(L'X', file), wint_t(L'X'));
    wchar_t wc = 0;
    EXPECT_EQ(LIBC_NAMESPACE::fwscanf(file, L"%lc", &wc), 1);
    EXPECT_EQ(wc, L'X');
    EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L'\u20ac'));
    EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L'!'));
    EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
  }
}

TEST(LlvmLibcWideScanTest, StreamPrefixFailurePreservesNextCharacter) {
  ::FILE *file = input_file("0x1p 12", 7);
  ASSERT_NE(file, nullptr);
  double value = -1;
  EXPECT_EQ(LIBC_NAMESPACE::fwscanf(file, L"%lf", &value), 0);
  EXPECT_TRUE(value == -1);
  EXPECT_EQ(LIBC_NAMESPACE::ftell(file), 4L);
  EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L' '));
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
  file = input_file(".e", 2);
  ASSERT_NE(file, nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::fwscanf(file, L"%lf", &value), 0);
  EXPECT_EQ(LIBC_NAMESPACE::ftell(file), 1L);
  EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(L'e'));
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}

TEST(LlvmLibcWideScanTest, EmbeddedNulAndStandardInput) {
  const char text[] = {'a', 0, 'b', ' ', '4', '2'};
  ::FILE *file = input_file(text, sizeof(text));
  ASSERT_NE(file, nullptr);
  ::FILE *saved = LIBC_NAMESPACE::stdin;
  LIBC_NAMESPACE::stdin = file;
  wchar_t wide[3] = {};
  int i = 0;
  EXPECT_EQ(LIBC_NAMESPACE::wscanf(L"%3lc", wide), 1);
  EXPECT_EQ(wide[0], L'a');
  EXPECT_EQ(wide[1], L'\0');
  EXPECT_EQ(wide[2], L'b');
  EXPECT_EQ(stdin_scan(L"%d", &i), 1);
  EXPECT_EQ(i, 42);
  LIBC_NAMESPACE::stdin = saved;
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}

using LlvmLibcWideScanErrorTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcWideScanErrorTest, ByteOrientationRejected) {
  ::FILE *file = input_file("1", 1);
  ASSERT_NE(file, nullptr);
  EXPECT_LT(LIBC_NAMESPACE::fwide(file, -1), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fwscanf(file, L""), EOF);
  ASSERT_ERRNO_EQ(EINVAL);
  EXPECT_NE(LIBC_NAMESPACE::ferror(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}

TEST_F(LlvmLibcWideScanErrorTest, EncodingError) {
  LocaleGuard locale;
  ::FILE *file = input_file("\xff", 1);
  ASSERT_NE(file, nullptr);
  wchar_t c;
  EXPECT_EQ(LIBC_NAMESPACE::fwscanf(file, L"%lc", &c), EOF);
  ASSERT_ERRNO_EQ(EILSEQ);
  EXPECT_NE(LIBC_NAMESPACE::ferror(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
  char bytes[4];
  const wchar_t invalid[] = {wchar_t(0xd800), 0};
  EXPECT_EQ(scan(invalid, L"%c", bytes), EOF);
  ASSERT_ERRNO_EQ(EILSEQ);
}

TEST_F(LlvmLibcWideScanErrorTest, ReadError) {
  auto name = libc_make_test_file_path("wide-scan-write-only");
  ::FILE *file = LIBC_NAMESPACE::fopen(name, "w");
  ASSERT_NE(file, nullptr);
  int value = 17;
  EXPECT_EQ(LIBC_NAMESPACE::fwscanf(file, L"%d", &value), EOF);
  ASSERT_ERRNO_EQ(EBADF);
  EXPECT_EQ(value, 17);
  EXPECT_NE(LIBC_NAMESPACE::ferror(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}
