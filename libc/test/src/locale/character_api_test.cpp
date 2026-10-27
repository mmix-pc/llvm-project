//===-- Locale character API tests ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/wchar_macros.h"
#include "src/__support/libc_errno.h"
#include "src/ctype/isalnum.h"
#include "src/ctype/isalnum_l.h"
#include "src/ctype/isalpha.h"
#include "src/ctype/isalpha_l.h"
#include "src/ctype/isblank.h"
#include "src/ctype/isblank_l.h"
#include "src/ctype/iscntrl.h"
#include "src/ctype/iscntrl_l.h"
#include "src/ctype/isdigit.h"
#include "src/ctype/isdigit_l.h"
#include "src/ctype/isgraph.h"
#include "src/ctype/isgraph_l.h"
#include "src/ctype/islower.h"
#include "src/ctype/islower_l.h"
#include "src/ctype/isprint.h"
#include "src/ctype/isprint_l.h"
#include "src/ctype/ispunct.h"
#include "src/ctype/ispunct_l.h"
#include "src/ctype/isspace.h"
#include "src/ctype/isspace_l.h"
#include "src/ctype/isupper.h"
#include "src/ctype/isupper_l.h"
#include "src/ctype/isxdigit.h"
#include "src/ctype/isxdigit_l.h"
#include "src/ctype/tolower.h"
#include "src/ctype/tolower_l.h"
#include "src/ctype/toupper.h"
#include "src/ctype/toupper_l.h"
#include "src/locale/locale_storage.h"
#include "src/locale/character.h"
#include "src/locale/uselocale.h"
#include "src/strings/strcasecmp.h"
#include "src/strings/strcasecmp_l.h"
#include "src/strings/strncasecmp.h"
#include "src/strings/strncasecmp_l.h"
#include "src/wctype/iswalnum.h"
#include "src/wctype/iswalnum_l.h"
#include "src/wctype/iswalpha.h"
#include "src/wctype/iswalpha_l.h"
#include "src/wctype/iswblank.h"
#include "src/wctype/iswblank_l.h"
#include "src/wctype/iswcntrl.h"
#include "src/wctype/iswcntrl_l.h"
#include "src/wctype/iswctype.h"
#include "src/wctype/iswctype_l.h"
#include "src/wctype/iswdigit.h"
#include "src/wctype/iswdigit_l.h"
#include "src/wctype/iswgraph.h"
#include "src/wctype/iswgraph_l.h"
#include "src/wctype/iswlower.h"
#include "src/wctype/iswlower_l.h"
#include "src/wctype/iswprint.h"
#include "src/wctype/iswprint_l.h"
#include "src/wctype/iswpunct.h"
#include "src/wctype/iswpunct_l.h"
#include "src/wctype/iswspace.h"
#include "src/wctype/iswspace_l.h"
#include "src/wctype/iswupper.h"
#include "src/wctype/iswupper_l.h"
#include "src/wctype/iswxdigit.h"
#include "src/wctype/iswxdigit_l.h"
#include "src/wctype/towctrans.h"
#include "src/wctype/towctrans_l.h"
#include "src/wctype/towlower.h"
#include "src/wctype/towlower_l.h"
#include "src/wctype/towupper.h"
#include "src/wctype/towupper_l.h"
#include "src/wctype/wctrans.h"
#include "src/wctype/wctrans_l.h"
#include "src/wctype/wctype_impl.h"
#include "src/wctype/wctype_l.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::libc_errno;

TEST(LlvmLibcCharacterLocaleTest, BuiltinCaseMappingSources) {
  __locale_data ctype_c("C", false), ctype_utf8("C.UTF-8", true);
  auto accept = [](wint_t source, void *expected) {
    return source == *static_cast<wint_t *>(expected);
  };
  wint_t expected = 0xb5;
  EXPECT_TRUE(case_source_matches(0x39c, true, &ctype_utf8, accept, &expected));
  EXPECT_FALSE(case_source_matches(0x39c, false, &ctype_utf8, accept, &expected));
  EXPECT_FALSE(case_source_matches(0x39c, true, &ctype_c, accept, &expected));
  expected = 'a';
  EXPECT_TRUE(case_source_matches('A', true, &ctype_c, accept, &expected));
  EXPECT_FALSE(case_source_matches(WEOF, true, &ctype_utf8, accept, &expected));
  expected = 0x10428;
  EXPECT_TRUE(case_source_matches(0x10400, true, &ctype_utf8, accept, &expected));
}

namespace {
class Selection {
  locale_t previous;

public:
  explicit Selection(locale_t locale)
      : previous(LIBC_NAMESPACE::uselocale(locale)) {}
  ~Selection() { LIBC_NAMESPACE::uselocale(previous); }
  Selection(const Selection &) = delete;
  Selection &operator=(const Selection &) = delete;
};

// This deliberately small private fixture is not an installed regional locale.
// It tests validated-record dispatch, not public newlocale name resolution.
struct Fixture {
  uint32_t words[14][64]{};
  size_t counts[14]{};
  uint8_t bytes[4096]{};
  __locale_t object = locale_from_mask(0);
  __locale_data *category = nullptr;

  static void word(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - 8 * i));
  }
  void pair(unsigned field, uint32_t first, uint32_t last) {
    words[field][counts[field]++] = first;
    words[field][counts[field]++] = last;
  }
  Fixture() {
    using D = CTypeLocaleData;
    pair(D::UPPER, 'A', 'Z');
    pair(D::UPPER, 0xc4, 0xc4);
    pair(D::UPPER, 0x130, 0x130);
    pair(D::LOWER, 'a', 'z');
    pair(D::LOWER, 0xe4, 0xe4);
    pair(D::LOWER, 0x131, 0x131);
    // '@' is deliberately alpha, not punct, to catch ASCII bypasses.
    pair(D::ALPHA, '@', 'Z');
    pair(D::ALPHA, 'a', 'z');
    pair(D::ALPHA, 0xc4, 0xc4);
    pair(D::ALPHA, 0xe4, 0xe4);
    pair(D::ALPHA, 0x130, 0x131);
    pair(D::DIGIT, '0', '9');
    pair(D::SPACE, 9, 13);
    pair(D::SPACE, 32, 32);
    pair(D::CNTRL, 0, 31);
    pair(D::CNTRL, 127, 127);
    pair(D::PUNCT, 33, 47);
    pair(D::PUNCT, 58, 63);
    pair(D::PUNCT, 91, 96);
    pair(D::PUNCT, 123, 126);
    const unsigned visible[] = {D::GRAPH, D::PRINT};
    for (unsigned field : visible) {
      pair(field, field == D::GRAPH ? 33 : 32, 126);
      pair(field, 0xc4, 0xc4);
      pair(field, 0xe4, 0xe4);
      pair(field, 0x130, 0x131);
    }
    pair(D::XDIGIT, '0', '9');
    pair(D::XDIGIT, 'A', 'F');
    pair(D::XDIGIT, 'a', 'f');
    pair(D::BLANK, 9, 9);
    pair(D::BLANK, 32, 32);
    for (unsigned c = 'a'; c <= 'z'; ++c)
      pair(11, c, c == 'i' ? 0x130 : c - 'a' + 'A');
    pair(11, 0xe4, 0xc4);
    pair(11, 0x131, 'I');
    for (unsigned c = 'A'; c <= 'Z'; ++c)
      pair(12, c, c == 'I' ? 0x131 : c - 'A' + 'a');
    pair(12, 0xc4, 0xe4);
    pair(12, 0x130, 'i');
  }
  bool create(bool stateful = false) {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    word(bytes + 8, 1);
    word(bytes + 12, 0x01020304);
    word(bytes + 20, 6);
    word(bytes + 24, 128);
    word(bytes + 28, 2);
    bytes[128] = 'x';
    unsigned entries = stateful ? 21 : 15;
    size_t offset = entries * 16;
    auto *section = bytes + 130;
    for (unsigned i = 0; i != 14; ++i) {
      auto *entry = section + i * 16;
      word(entry, i < 11 ? i + 1 : 0x101 + i - 11);
      word(entry + 4, 2);
      word(entry + 8, uint32_t(offset));
      word(entry + 12, uint32_t(counts[i] * 4));
      for (size_t j = 0; j != counts[i]; ++j)
        word(section + offset + 4 * j, words[i][j]);
      offset += counts[i] * 4;
    }
    auto *encoding = section + 14 * 16;
    word(encoding, 0x200);
    word(encoding + 4, 1);
    word(encoding + 8, uint32_t(offset));
    const char *codeset = stateful ? "ISO-2022-CN" : "UTF-8";
    size_t codeset_size = stateful ? sizeof("ISO-2022-CN") : sizeof("UTF-8");
    word(encoding + 12, uint32_t(codeset_size));
    for (unsigned i = 0; i != codeset_size; ++i)
      section[offset++] = uint8_t(codeset[i]);
    if (stateful) {
      // Minimal, validated two-byte mappings for the wide case pair.
      for (unsigned i = 0; i != 6; ++i) {
        auto *entry = section + (15 + i) * 16;
        word(entry, 0x201 + i);
        word(entry + 4, 2);
        word(entry + 8, uint32_t(offset));
        word(entry + 12, 16);
        bool encode = i % 2;
        word(section + offset, encode ? 0xc4 : 0x2121);
        word(section + offset + 4, encode ? 0x2121 : 0xc4);
        word(section + offset + 8, encode ? 0xe4 : 0x2122);
        word(section + offset + 12, encode ? 0x2122 : 0xe4);
        offset += 16;
      }
    }
    size_t end = 130 + offset;
    word(bytes + 16, uint32_t(end));
    for (unsigned i = 0; i != 6; ++i) {
      auto *entry = bytes + 32 + i * 16;
      word(entry, i);
      word(entry + 4, i ? uint32_t(end) : 130);
      word(entry + 8, i ? 0 : uint32_t(offset));
      word(entry + 12, i ? 0 : entries);
    }
    auto file = LocaleDataFile::copy({bytes, end});
    if (!file)
      return false;
    auto record = create_ctype_category(**file);
    (*file)->release();
    if (!record)
      return false;
    category = *record;
    object.data[LC_CTYPE] = category;
    return true;
  }
  ~Fixture() {
    if (category)
      release_locale_category(category);
  }
};

TEST(LlvmLibcCharacterLocaleTest, RegionalCaseMappingSources) {
  Fixture fixture;
  ASSERT_TRUE(fixture.create());
  auto accept = [](wint_t source, void *expected) {
    return source == *static_cast<wint_t *>(expected);
  };
  wint_t expected = 'i';
  EXPECT_TRUE(case_source_matches(0x130, true, fixture.category, accept, &expected));
  EXPECT_FALSE(case_source_matches('I', true, fixture.category, accept, &expected));
  expected = 0x131;
  EXPECT_TRUE(case_source_matches('I', true, fixture.category, accept, &expected));
  expected = 'I';
  EXPECT_TRUE(case_source_matches(0x131, false, fixture.category, accept, &expected));
}

struct Classification {
  const char *name;
  int (*byte)(int);
  int (*byte_l)(int, locale_t);
  int (*wide)(wint_t);
  int (*wide_l)(wint_t, locale_t);
};
const Classification classes[] = {
    {"alnum", LIBC_NAMESPACE::isalnum, LIBC_NAMESPACE::isalnum_l,
     LIBC_NAMESPACE::iswalnum, LIBC_NAMESPACE::iswalnum_l},
    {"alpha", LIBC_NAMESPACE::isalpha, LIBC_NAMESPACE::isalpha_l,
     LIBC_NAMESPACE::iswalpha, LIBC_NAMESPACE::iswalpha_l},
    {"blank", LIBC_NAMESPACE::isblank, LIBC_NAMESPACE::isblank_l,
     LIBC_NAMESPACE::iswblank, LIBC_NAMESPACE::iswblank_l},
    {"cntrl", LIBC_NAMESPACE::iscntrl, LIBC_NAMESPACE::iscntrl_l,
     LIBC_NAMESPACE::iswcntrl, LIBC_NAMESPACE::iswcntrl_l},
    {"digit", LIBC_NAMESPACE::isdigit, LIBC_NAMESPACE::isdigit_l,
     LIBC_NAMESPACE::iswdigit, LIBC_NAMESPACE::iswdigit_l},
    {"graph", LIBC_NAMESPACE::isgraph, LIBC_NAMESPACE::isgraph_l,
     LIBC_NAMESPACE::iswgraph, LIBC_NAMESPACE::iswgraph_l},
    {"lower", LIBC_NAMESPACE::islower, LIBC_NAMESPACE::islower_l,
     LIBC_NAMESPACE::iswlower, LIBC_NAMESPACE::iswlower_l},
    {"print", LIBC_NAMESPACE::isprint, LIBC_NAMESPACE::isprint_l,
     LIBC_NAMESPACE::iswprint, LIBC_NAMESPACE::iswprint_l},
    {"punct", LIBC_NAMESPACE::ispunct, LIBC_NAMESPACE::ispunct_l,
     LIBC_NAMESPACE::iswpunct, LIBC_NAMESPACE::iswpunct_l},
    {"space", LIBC_NAMESPACE::isspace, LIBC_NAMESPACE::isspace_l,
     LIBC_NAMESPACE::iswspace, LIBC_NAMESPACE::iswspace_l},
    {"upper", LIBC_NAMESPACE::isupper, LIBC_NAMESPACE::isupper_l,
     LIBC_NAMESPACE::iswupper, LIBC_NAMESPACE::iswupper_l},
    {"xdigit", LIBC_NAMESPACE::isxdigit, LIBC_NAMESPACE::isxdigit_l,
     LIBC_NAMESPACE::iswxdigit, LIBC_NAMESPACE::iswxdigit_l},
};

bool ascii_expected(unsigned index, int c) {
  bool upper = c >= 'A' && c <= 'Z';
  bool lower = c >= 'a' && c <= 'z';
  bool digit = c >= '0' && c <= '9';
  bool graph = c >= 33 && c <= 126;
  switch (index) {
  case 0:
    return upper || lower || digit;
  case 1:
    return upper || lower;
  case 2:
    return c == 9 || c == 32;
  case 3:
    return (c >= 0 && c <= 31) || c == 127;
  case 4:
    return digit;
  case 5:
    return graph;
  case 6:
    return lower;
  case 7:
    return c >= 32 && c <= 126;
  case 8:
    return graph && !(upper || lower || digit);
  case 9:
    return (c >= 9 && c <= 13) || c == 32;
  case 10:
    return upper;
  case 11:
    return digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  default:
    return false;
  }
}
} // namespace

TEST(LlvmLibcCharacterAPITest, BuiltinByteDomainAndCWideDomain) {
  auto c_locale = locale_from_mask(0);
  auto utf8 = locale_from_mask(1U << LC_CTYPE);
  locale_t locales[] = {&c_locale, &utf8};
  for (auto locale : locales) {
    Selection selected(locale);
    for (int c = EOF; c <= 255; ++c) {
      for (unsigned i = 0; i != 12; ++i) {
        bool expected = ascii_expected(i, c);
        EXPECT_EQ(classes[i].byte(c) != 0, expected);
        EXPECT_EQ(classes[i].byte_l(c, locale) != 0, expected);
        if (locale == &c_locale) {
          wint_t wc = c == EOF ? WEOF : wint_t(c);
          EXPECT_EQ(classes[i].wide(wc) != 0, expected);
          EXPECT_EQ(classes[i].wide_l(wc, locale) != 0, expected);
        }
      }
      int upper = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
      int lower = c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
      EXPECT_EQ(LIBC_NAMESPACE::toupper(c), upper);
      EXPECT_EQ(LIBC_NAMESPACE::toupper_l(c, locale), upper);
      EXPECT_EQ(LIBC_NAMESPACE::tolower(c), lower);
      EXPECT_EQ(LIBC_NAMESPACE::tolower_l(c, locale), lower);
      if (locale == &c_locale) {
        wint_t wc = c == EOF ? WEOF : wint_t(c);
        EXPECT_EQ(LIBC_NAMESPACE::towupper(wc),
                  c == EOF ? WEOF : wint_t(upper));
        EXPECT_EQ(LIBC_NAMESPACE::towlower(wc),
                  c == EOF ? WEOF : wint_t(lower));
      }
    }
  }
}

TEST(LlvmLibcCharacterAPITest, RegionalByteDomainAndDescriptors) {
  Fixture f;
  ASSERT_TRUE(f.create());
  Selection selected(&f.object);
  for (int c = EOF; c <= 255; ++c) {
    for (unsigned i = 0; i != 12; ++i) {
      bool expected = ascii_expected(i, c);
      if (c == '@' && (i == 0 || i == 1 || i == 8))
        expected = i != 8;
      EXPECT_EQ(classes[i].byte(c) != 0, expected);
      EXPECT_EQ(classes[i].byte_l(c, &f.object) != 0, expected);
      if (c < 128) {
        wint_t wc = c == EOF ? WEOF : wint_t(c);
        auto desc = LIBC_NAMESPACE::wctype_l(classes[i].name, &f.object);
        EXPECT_NE(desc, wctype_t(0));
        EXPECT_EQ(classes[i].wide(wc) != 0, expected);
        EXPECT_EQ(classes[i].wide_l(wc, &f.object) != 0, expected);
        EXPECT_EQ(LIBC_NAMESPACE::iswctype(wc, desc) != 0, expected);
        EXPECT_EQ(LIBC_NAMESPACE::iswctype_l(wc, desc, &f.object) != 0,
                  expected);
      }
    }
    int upper = c >= 'a' && c <= 'z' && c != 'i' ? c - 'a' + 'A' : c;
    int lower = c >= 'A' && c <= 'Z' && c != 'I' ? c - 'A' + 'a' : c;
    EXPECT_EQ(LIBC_NAMESPACE::toupper(c), upper);
    EXPECT_EQ(LIBC_NAMESPACE::toupper_l(c, &f.object), upper);
    EXPECT_EQ(LIBC_NAMESPACE::tolower(c), lower);
    EXPECT_EQ(LIBC_NAMESPACE::tolower_l(c, &f.object), lower);
  }
  const wint_t letters[] = {0xc4, 0xe4, 0x130, 0x131};
  for (wint_t wc : letters) {
    bool upper = wc == 0xc4 || wc == 0x130;
    for (unsigned i = 0; i != 12; ++i) {
      bool expected = i == 0 || i == 1 || i == 5 || i == 7 ||
                      (i == 6 && !upper) || (i == 10 && upper);
      auto desc = LIBC_NAMESPACE::wctype_l(classes[i].name, &f.object);
      EXPECT_EQ(classes[i].wide(wc) != 0, expected);
      EXPECT_EQ(classes[i].wide_l(wc, &f.object) != 0, expected);
      EXPECT_EQ(LIBC_NAMESPACE::iswctype(wc, desc) != 0, expected);
      EXPECT_EQ(LIBC_NAMESPACE::iswctype_l(wc, desc, &f.object) != 0, expected);
    }
  }
}

TEST(LlvmLibcCharacterAPITest, StatefulByteDomainAndWideMappings) {
  Fixture f;
  ASSERT_TRUE(f.create(true));
  ASSERT_TRUE(f.category->chinese_encoding.has_value());
  EXPECT_FALSE(f.category->utf8);
  auto c_locale = locale_from_mask(0);
  {
    Selection selected(&f.object);
    libc_errno = EDOM;
    for (int c = EOF; c <= 255; ++c) {
      for (unsigned i = 0; i != 12; ++i) {
        bool expected = ascii_expected(i, c);
        if (c == '@' && (i == 0 || i == 1 || i == 8))
          expected = i != 8;
        EXPECT_EQ(classes[i].byte(c) != 0, expected);
        EXPECT_EQ(classes[i].byte_l(c, &f.object) != 0, expected);
      }
      int upper = c >= 'a' && c <= 'z' && c != 'i' ? c - 'a' + 'A' : c;
      int lower = c >= 'A' && c <= 'Z' && c != 'I' ? c - 'A' + 'a' : c;
      EXPECT_EQ(LIBC_NAMESPACE::toupper(c), upper);
      EXPECT_EQ(LIBC_NAMESPACE::toupper_l(c, &f.object), upper);
      EXPECT_EQ(LIBC_NAMESPACE::tolower(c), lower);
      EXPECT_EQ(LIBC_NAMESPACE::tolower_l(c, &f.object), lower);
    }
    // A valid wide scalar need not have a single-byte encoding.
    EXPECT_NE(LIBC_NAMESPACE::iswlower(0xe4), 0);
    EXPECT_NE(LIBC_NAMESPACE::iswupper_l(0xc4, &f.object), 0);
    EXPECT_EQ(LIBC_NAMESPACE::towupper(0xe4), wint_t(0xc4));
    EXPECT_EQ(LIBC_NAMESPACE::towlower_l(0xc4, &f.object), wint_t(0xe4));
    EXPECT_EQ(LIBC_NAMESPACE::isalpha_l('@', &c_locale), 0);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  Selection selected(&c_locale);
  EXPECT_EQ(LIBC_NAMESPACE::isalpha('@'), 0);
  EXPECT_NE(LIBC_NAMESPACE::isalpha_l('@', &f.object), 0);
  EXPECT_EQ(LIBC_NAMESPACE::toupper_l(0xe4, &f.object), 0xe4);
  EXPECT_EQ(LIBC_NAMESPACE::uselocale(nullptr), &c_locale);
}

TEST(LlvmLibcCharacterAPITest, ExplicitSelectionDoesNotChangeThread) {
  Fixture f;
  ASSERT_TRUE(f.create());
  auto c_locale = locale_from_mask(0);
  auto utf8 = locale_from_mask(1U << LC_CTYPE);
  Selection selected(&c_locale);
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::towupper_l('i', &f.object), wint_t(0x130));
  EXPECT_EQ(LIBC_NAMESPACE::towlower_l('I', &f.object), wint_t(0x131));
  EXPECT_EQ(LIBC_NAMESPACE::towupper_l(0x131, &f.object), wint_t('I'));
  EXPECT_EQ(LIBC_NAMESPACE::towlower_l(0x130, &f.object), wint_t('i'));
  EXPECT_EQ(LIBC_NAMESPACE::towupper('i'), wint_t('I'));
  EXPECT_EQ(LIBC_NAMESPACE::towlower('I'), wint_t('i'));
  EXPECT_EQ(LIBC_NAMESPACE::towupper_l(0xe4, &utf8), wint_t(0xc4));
  EXPECT_EQ(LIBC_NAMESPACE::towlower_l(0xc4, &utf8), wint_t(0xe4));
  EXPECT_NE(LIBC_NAMESPACE::iswalpha_l(0xe4, &utf8), 0);
  EXPECT_NE(LIBC_NAMESPACE::iswspace_l(0x2003, &utf8), 0);
  EXPECT_EQ(LIBC_NAMESPACE::iswdigit_l(0x660, &utf8), 0);
  EXPECT_EQ(LIBC_NAMESPACE::iswxdigit_l(0xff21, &utf8), 0);
  EXPECT_EQ(LIBC_NAMESPACE::iswalpha(0xe4), 0);
  EXPECT_NE(LIBC_NAMESPACE::isalpha_l('@', &f.object), 0);
  EXPECT_EQ(LIBC_NAMESPACE::isalpha('@'), 0);
  EXPECT_EQ(LIBC_NAMESPACE::toupper_l('i', &f.object), int('i'));
  EXPECT_EQ(LIBC_NAMESPACE::tolower_l('I', &f.object), int('I'));
  EXPECT_EQ(LIBC_NAMESPACE::uselocale(nullptr), &c_locale);
  EXPECT_EQ(int(libc_errno), EDOM);
  {
    Selection regional(&f.object);
    EXPECT_EQ(LIBC_NAMESPACE::towupper('i'), wint_t(0x130));
    EXPECT_EQ(LIBC_NAMESPACE::towlower('I'), wint_t(0x131));
    EXPECT_EQ(LIBC_NAMESPACE::towupper_l('i', &utf8), wint_t('I'));
    EXPECT_EQ(LIBC_NAMESPACE::towlower_l('I', &utf8), wint_t('i'));
    EXPECT_EQ(LIBC_NAMESPACE::iswalpha_l(0xe4, &c_locale), 0);
    EXPECT_EQ(LIBC_NAMESPACE::towupper_l(0xe4, &c_locale), wint_t(0xe4));
    EXPECT_NE(LIBC_NAMESPACE::iswalpha(0xe4), 0);
    EXPECT_EQ(LIBC_NAMESPACE::uselocale(nullptr), &f.object);
  }
}

TEST(LlvmLibcCharacterAPITest, WideDescriptorsAndSentinels) {
  Fixture f;
  ASSERT_TRUE(f.create());
  Selection selected(&f.object);
  auto lower = LIBC_NAMESPACE::wctrans_l("tolower", &f.object);
  auto upper = LIBC_NAMESPACE::wctrans_l("toupper", &f.object);
  EXPECT_EQ(lower, LIBC_NAMESPACE::wctrans("tolower"));
  EXPECT_EQ(upper, LIBC_NAMESPACE::wctrans("toupper"));
  EXPECT_NE(lower, upper);
  EXPECT_EQ(LIBC_NAMESPACE::towctrans_l('I', lower, &f.object), wint_t(0x131));
  EXPECT_EQ(LIBC_NAMESPACE::towctrans_l('i', upper, &f.object), wint_t(0x130));
  EXPECT_EQ(LIBC_NAMESPACE::towctrans('I', lower), wint_t(0x131));
  EXPECT_EQ(LIBC_NAMESPACE::towctrans('i', upper), wint_t(0x130));
  EXPECT_EQ(LIBC_NAMESPACE::wctype_l("unknown", &f.object), wctype_t(0));
  EXPECT_EQ(LIBC_NAMESPACE::wctrans_l("unknown", &f.object), wctrans_t(0));
  EXPECT_EQ(LIBC_NAMESPACE::iswctype_l('A', 0, &f.object), 0);
  EXPECT_EQ(LIBC_NAMESPACE::towctrans_l('A', wctrans_t(0), &f.object),
            wint_t('A'));
  const wint_t invalid[] = {WEOF, 0xd800, 0x110000};
  for (wint_t wc : invalid) {
    for (const auto &kind : classes) {
      auto desc = LIBC_NAMESPACE::wctype_l(kind.name, &f.object);
      EXPECT_EQ(desc, LIBC_NAMESPACE::wctype(kind.name));
      EXPECT_EQ(kind.wide(wc), 0);
      EXPECT_EQ(kind.wide_l(wc, &f.object), 0);
      EXPECT_EQ(LIBC_NAMESPACE::iswctype(wc, desc), 0);
      EXPECT_EQ(LIBC_NAMESPACE::iswctype_l(wc, desc, &f.object), 0);
    }
    EXPECT_EQ(LIBC_NAMESPACE::towupper(wc), wc);
    EXPECT_EQ(LIBC_NAMESPACE::towlower(wc), wc);
    EXPECT_EQ(LIBC_NAMESPACE::towupper_l(wc, &f.object), wc);
    EXPECT_EQ(LIBC_NAMESPACE::towlower_l(wc, &f.object), wc);
    EXPECT_EQ(LIBC_NAMESPACE::towctrans_l(wc, upper, &f.object), wc);
    EXPECT_EQ(LIBC_NAMESPACE::towctrans(wc, lower), wc);
  }
  // A valid unlisted scalar has no classes and an identity mapping.
  EXPECT_EQ(LIBC_NAMESPACE::iswalpha_l(0x10400, &f.object), 0);
  EXPECT_EQ(LIBC_NAMESPACE::towlower_l(0x10400, &f.object), wint_t(0x10400));
}

TEST(LlvmLibcCharacterAPITest, ByteComparisonsUseSelectedCaseRules) {
  Fixture f;
  ASSERT_TRUE(f.create());
  auto c_locale = locale_from_mask(0);
  Selection selected(&c_locale);
  EXPECT_EQ(LIBC_NAMESPACE::strcasecmp("I", "i"), 0);
  EXPECT_LT(LIBC_NAMESPACE::strcasecmp_l("I", "i", &f.object), 0);
  EXPECT_EQ(LIBC_NAMESPACE::strncasecmp_l("A1", "a2", 1, &f.object), 0);
  EXPECT_EQ(LIBC_NAMESPACE::strncasecmp_l("I", "i", 0, &f.object), 0);
  const char left[] = {'A', 'I'}, right[] = {'a', 'i'};
  EXPECT_EQ(LIBC_NAMESPACE::strncasecmp(left, right, 2), 0);
  EXPECT_LT(LIBC_NAMESPACE::strncasecmp_l(left, right, 2, &f.object), 0);
  {
    Selection regional(&f.object);
    EXPECT_LT(LIBC_NAMESPACE::strcasecmp("I", "i"), 0);
    EXPECT_EQ(LIBC_NAMESPACE::strcasecmp_l("I", "i", &c_locale), 0);
    EXPECT_EQ(LIBC_NAMESPACE::strncasecmp_l(left, right, 2, &c_locale), 0);
    EXPECT_EQ(LIBC_NAMESPACE::uselocale(nullptr), &f.object);
  }
  for (unsigned value = 128; value <= 255; ++value) {
    char high[] = {static_cast<char>(value), 0};
    EXPECT_GT(LIBC_NAMESPACE::strcasecmp_l(high, "z", &c_locale), 0);
    EXPECT_GT(LIBC_NAMESPACE::strncasecmp_l(high, "z", 1, &f.object), 0);
    EXPECT_EQ(LIBC_NAMESPACE::strcasecmp_l(high, high, &f.object), 0);
  }
}
