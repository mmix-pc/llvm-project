//===-- Built-in locale behavior tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/app.h"
#include "hdr/errno_macros.h"
#include "hdr/langinfo_macros.h"
#include "hdr/locale_macros.h"
#include "src/__support/CPP/string.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/libc_errno.h"
#include "src/langinfo/nl_langinfo.h"
#include "src/langinfo/nl_langinfo_l.h"
#include "src/locale/duplocale.h"
#include "src/locale/freelocale.h"
#include "src/locale/mb_cur_max.h"
#include "src/locale/newlocale.h"
#include "src/locale/setlocale.h"
#include "src/locale/uselocale.h"
#include "src/stdlib/getenv.h"
#include "src/stdlib/setenv.h"
#include "src/stdlib/unsetenv.h"
#include "src/string/strerror_l.h"
#include "src/uchar/c32rtomb.h"
#include "src/uchar/mbrtoc32.h"
#include "src/wchar/mbrtowc.h"
#include "src/wchar/swprintf.h"
#include "src/wchar/wcrtomb.h"
#include "src/wctype/iswalpha.h"
#include "src/wctype/towlower.h"
#include "test/UnitTest/Test.h"

namespace LIBC_NAMESPACE_DECL {
AppProperties app;
}

namespace {
using namespace LIBC_NAMESPACE;
class LlvmLibcBuiltinLocaleTest : public testing::Test {
  cpp::string saved_global;
  locale_t saved_thread;

public:
  void SetUp() override {
    saved_global = cpp::string_view(LIBC_NAMESPACE::setlocale(LC_ALL, nullptr));
    saved_thread = LIBC_NAMESPACE::uselocale(LC_GLOBAL_LOCALE);
    ASSERT_NE(LIBC_NAMESPACE::setlocale(LC_ALL, "C"), nullptr);
  }
  void TearDown() override {
    LIBC_NAMESPACE::setlocale(LC_ALL, saved_global.c_str());
    LIBC_NAMESPACE::uselocale(saved_thread);
  }
};

TEST_F(LlvmLibcBuiltinLocaleTest, CategoriesAliasesAndRollback) {
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_ALL, nullptr), "C");
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_CTYPE, "C.utf8"), "C.UTF-8");
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_NUMERIC, nullptr), "C");
  cpp::string mixed(LIBC_NAMESPACE::setlocale(LC_ALL, nullptr));
  EXPECT_NE(LIBC_NAMESPACE::setlocale(LC_ALL, "POSIX"), nullptr);
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_ALL, mixed.c_str()), mixed.c_str());
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(CODESET), "UTF-8");
  EXPECT_EQ(LIBC_NAMESPACE::setlocale(LC_ALL, "en_US.UTF-8"), nullptr);
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_ALL, nullptr), mixed.c_str());
  EXPECT_EQ(LIBC_NAMESPACE::setlocale(-1, "C"), nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::setlocale(LC_ALL + 1, nullptr), nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::setlocale(LC_ALL, "LC_CTYPE=C;LC_NUMERIC=bad"),
            nullptr);
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_ALL, nullptr), mixed.c_str());
}

TEST_F(LlvmLibcBuiltinLocaleTest, ObjectsAndGlobalSnapshots) {
  locale_t loc = LIBC_NAMESPACE::newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
  ASSERT_NE(loc, nullptr);
  locale_t copy = LIBC_NAMESPACE::duplocale(loc);
  ASSERT_NE(copy, nullptr);
  EXPECT_NE(copy, loc);
  EXPECT_EQ(LIBC_NAMESPACE::uselocale(loc), LC_GLOBAL_LOCALE);
  EXPECT_EQ(LIBC_NAMESPACE::uselocale(nullptr), loc);
  EXPECT_EQ(LIBC_NAMESPACE::__ctype_get_mb_cur_max(), size_t(4));
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_CTYPE, nullptr), "C");
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(LC_CTYPE_MASK, "unsupported", copy),
            nullptr);
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(CODESET, copy), "UTF-8");
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(1 << 20, "C", nullptr), nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(LC_ALL_MASK, nullptr, nullptr), nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C", LC_GLOBAL_LOCALE),
            nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(LC_CTYPE_MASK, "C", copy), copy);
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(CODESET, copy), "ANSI_X3.4-1968");
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(CODESET, loc), "UTF-8");
  EXPECT_EQ(LIBC_NAMESPACE::uselocale(LC_GLOBAL_LOCALE), loc);
  LIBC_NAMESPACE::freelocale(loc);
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(CODESET, copy), "ANSI_X3.4-1968");
  LIBC_NAMESPACE::freelocale(copy);
  copy = LIBC_NAMESPACE::duplocale(LC_GLOBAL_LOCALE);
  ASSERT_NE(copy, nullptr);
  EXPECT_NE(LIBC_NAMESPACE::setlocale(LC_ALL, "C.UTF-8"), nullptr);
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(CODESET, copy), "ANSI_X3.4-1968");
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(CODESET), "UTF-8");
  LIBC_NAMESPACE::freelocale(copy);
}

TEST_F(LlvmLibcBuiltinLocaleTest, LanginfoData) {
  const char *names[] = {"C", "C.UTF-8"};
  for (const char *name : names) {
    locale_t loc = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, name, nullptr);
    ASSERT_NE(loc, nullptr);
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(DAY_1, loc), "Sunday");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(DAY_7, loc), "Saturday");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(ABDAY_2, loc), "Mon");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(MON_12, loc), "December");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(ABMON_1, loc), "Jan");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(D_T_FMT, loc),
                 "%a %b %e %H:%M:%S %Y");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(RADIXCHAR, loc), ".");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(THOUSEP, loc), "");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(CRNCYSTR, loc), "");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(ERA, loc), "");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(ALT_DIGITS, loc), "");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(YESEXPR, loc), "^[yY]");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(NOEXPR, loc), "^[nN]");
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(-1, loc), "");
    EXPECT_STREQ(LIBC_NAMESPACE::strerror_l(EINVAL, loc), "Invalid argument");
    LIBC_NAMESPACE::freelocale(loc);
  }
}

TEST_F(LlvmLibcBuiltinLocaleTest, CharacterEncodingAndClassification) {
  mbstate_t state{};
  wchar_t wc = 0;
  char bytes[4];
  wchar_t formatted[8];
  EXPECT_EQ(LIBC_NAMESPACE::__ctype_get_mb_cur_max(), size_t(1));
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&wc, "\xc3\xa9", 2, &state), size_t(-1));
  EXPECT_EQ(static_cast<int>(libc_errno), EILSEQ);
  state = {};
  EXPECT_EQ(LIBC_NAMESPACE::wcrtomb(bytes, 0xe9, &state), size_t(-1));
  EXPECT_EQ(LIBC_NAMESPACE::iswalpha(0xe9), 0);
  EXPECT_EQ(LIBC_NAMESPACE::towlower(0xc9), wint_t(0xc9));
  EXPECT_EQ(LIBC_NAMESPACE::swprintf(formatted, 8, L"%s", "\xc3\xa9"), -1);
  EXPECT_NE(LIBC_NAMESPACE::setlocale(LC_CTYPE, "C.UTF-8"), nullptr);
  state = {};
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&wc, "\xc3", 1, &state), size_t(-2));
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&wc, "\xa9", 1, &state), size_t(1));
  EXPECT_EQ(wc, wchar_t(0xe9));
  EXPECT_EQ(LIBC_NAMESPACE::wcrtomb(bytes, wc, &state), size_t(2));
  EXPECT_EQ(static_cast<unsigned char>(bytes[0]),
            static_cast<unsigned char>(0xc3));
  EXPECT_EQ(static_cast<unsigned char>(bytes[1]),
            static_cast<unsigned char>(0xa9));
  EXPECT_NE(LIBC_NAMESPACE::iswalpha(0xe9), 0);
  EXPECT_EQ(LIBC_NAMESPACE::towlower(0xc9), wint_t(0xe9));
  EXPECT_EQ(LIBC_NAMESPACE::swprintf(formatted, 8, L"%s", "\xc3\xa9"), 1);
  EXPECT_EQ(formatted[0], wchar_t(0xe9));
  char32_t scalar;
  EXPECT_EQ(LIBC_NAMESPACE::mbrtoc32(&scalar, "\xf0\x9f\x98\x80", 4, &state),
            size_t(4));
  EXPECT_EQ(static_cast<unsigned>(scalar), 0x1f600U);
  EXPECT_EQ(LIBC_NAMESPACE::c32rtomb(bytes, scalar, &state), size_t(4));
  EXPECT_EQ(LIBC_NAMESPACE::c32rtomb(bytes, 0xd800, &state), size_t(-1));
  state = {};
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&wc, "\xed\xa0\x80", 3, &state),
            size_t(-1));
  state = {};
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&wc, "\xc0\x80", 2, &state), size_t(-1));
  state = {};
  EXPECT_NE(LIBC_NAMESPACE::setlocale(LC_CTYPE, "C"), nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::c32rtomb(bytes, 0xe9, &state), size_t(-1));
}

TEST_F(LlvmLibcBuiltinLocaleTest, EnvironmentPrecedenceAndAtomicFailure) {
  const char *keys[] = {"LC_ALL",     "LC_CTYPE",    "LC_NUMERIC",  "LC_TIME",
                        "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES", "LANG"};
  cpp::string saved[8];
  bool present[8];
  for (unsigned i = 0; i < 8; ++i) {
    const char *value = LIBC_NAMESPACE::getenv(keys[i]);
    present[i] = value != nullptr;
    saved[i] = cpp::string_view(value ? value : "");
    LIBC_NAMESPACE::unsetenv(keys[i]);
  }
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_ALL, ""), "C");
  LIBC_NAMESPACE::setenv("LANG", "C.UTF-8", 1);
  LIBC_NAMESPACE::setenv("LC_NUMERIC", "POSIX", 1);
  EXPECT_NE(LIBC_NAMESPACE::setlocale(LC_ALL, ""), nullptr);
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_CTYPE, nullptr), "C.UTF-8");
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_NUMERIC, nullptr), "C");
  LIBC_NAMESPACE::setenv("LC_ALL", "C", 1);
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_ALL, ""), "C");
  LIBC_NAMESPACE::setenv("LC_ALL", "", 1);
  LIBC_NAMESPACE::setenv("LC_MESSAGES", "en_US", 1);
  EXPECT_EQ(LIBC_NAMESPACE::setlocale(LC_ALL, ""), nullptr);
  EXPECT_STREQ(LIBC_NAMESPACE::setlocale(LC_ALL, nullptr), "C");
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "", nullptr), nullptr);
  locale_t loc = LIBC_NAMESPACE::newlocale(LC_CTYPE_MASK, "", nullptr);
  EXPECT_NE(loc, nullptr);
  if (loc) {
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(CODESET, loc), "UTF-8");
    LIBC_NAMESPACE::freelocale(loc);
  }
  for (unsigned i = 0; i < 8; ++i) {
    if (present[i])
      LIBC_NAMESPACE::setenv(keys[i], saved[i].c_str(), 1);
    else
      LIBC_NAMESPACE::unsetenv(keys[i]);
  }
}
} // namespace
