//===-- Private locale data validation tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/locale/locale_data.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::LocaleDataFile;
using LIBC_NAMESPACE::internal::LocaleDataView;

namespace {
void put(uint8_t *p, uint32_t value) {
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

struct Fixture {
  // Deliberately unaligned, fixed-endian input independent of target layout.
  uint8_t allocation[1024]{};
  uint8_t *bytes = allocation + 1;
  size_t size = 135 + 6 * 65;
  Fixture() {
    const uint8_t magic[8] = {'L', 'L', 'V', 'M', 'L', 'C', 'L', 0};
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = magic[i];
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 16, uint32_t(size));
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 7);
    const char name[] = "sample";
    for (unsigned i = 0; i != sizeof(name); ++i)
      bytes[128 + i] = name[i];
    for (unsigned category = 0; category != 6; ++category) {
      uint8_t *d = bytes + 32 + category * 16;
      put(d, category);
      put(d + 4, 135 + category * 65);
      put(d + 8, 65);
      put(d + 12, 3);
      uint8_t *s = bytes + 135 + category * 65;
      put(s, 10);
      put(s + 4, 1);
      put(s + 8, 48);
      put(s + 12, 6);
      put(s + 16, 20);
      put(s + 20, 2);
      put(s + 24, 54);
      put(s + 28, 8);
      put(s + 32, 30);
      put(s + 36, 3);
      put(s + 40, 62);
      put(s + 44, 3);
      const char text[] = "hello";
      for (unsigned i = 0; i != sizeof(text); ++i)
        s[48 + i] = text[i];
      put(s + 54, 0x12345678);
      put(s + 58, category);
      s[62] = 0;
      s[63] = 0xff;
      s[64] = 0x80;
    }
  }

  void reference(unsigned category, uint32_t key, uint32_t kind) {
    size += 9;
    put(bytes + 16, uint32_t(size));
    put(bytes + 32 + 5 * 16 + 8, 74);
    uint8_t *section = bytes + 135 + 5 * 65;
    put(section + 36, 4);
    put(section + 44, 12);
    put(section + 62, category);
    put(section + 66, key);
    put(section + 70, kind);
  }
};
} // namespace

TEST(LlvmLibcLocaleDataTest, UnalignedBigEndianAndLookup) {
  Fixture f;
  auto parsed = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(parsed.has_value());
  EXPECT_STREQ(parsed->name(), "sample");
  for (unsigned category = 0; category != 6; ++category) {
    auto string = parsed->find(category, 10);
    ASSERT_TRUE(string.has_value());
    EXPECT_EQ(string->bytes.size(), size_t(6));
    EXPECT_EQ(string->bytes[4], uint8_t('o'));
    EXPECT_FALSE(string->word(0).has_value());
    auto words = parsed->find(category, 20);
    ASSERT_TRUE(words.has_value());
    EXPECT_EQ(words->word(0).value(), uint32_t(0x12345678));
    EXPECT_EQ(words->word(1).value(), category);
    EXPECT_FALSE(words->word(2).has_value());
    EXPECT_FALSE(words->word(size_t(-1)).has_value());
    EXPECT_EQ(parsed->find(category, 30)->bytes[1], uint8_t(0xff));
    EXPECT_EQ(parsed->find(category, 11).error(), ENOENT);
    EXPECT_EQ(parsed->find(category, 31).error(), ENOENT);
  }
  EXPECT_EQ(parsed->find(6, 10).error(), EINVAL);
  EXPECT_EQ(parsed->find(0, 0).error(), EINVAL);
}

TEST(LlvmLibcLocaleDataTest, TruncationAndHeaderValidation) {
  Fixture f;
  for (size_t size = 0; size < f.size; ++size) {
    // Also adjust the advertised size so section checks cannot rely on it.
    put(f.bytes + 16, uint32_t(size));
    EXPECT_FALSE(LocaleDataView::parse({f.bytes, size}).has_value());
  }
  EXPECT_FALSE(LocaleDataView::parse({nullptr, f.size}).has_value());
  EXPECT_FALSE(LocaleDataView::parse({f.bytes, size_t(-1)}).has_value());
  const unsigned fields[] = {0, 8, 12, 16, 20, 24, 28};
  for (unsigned field : fields) {
    Fixture invalid;
    put(invalid.bytes + field, 0xffffffff);
    EXPECT_FALSE(
        LocaleDataView::parse({invalid.bytes, invalid.size}).has_value());
  }
}

TEST(LlvmLibcLocaleDataTest, RejectRangesOrderingAndKinds) {
  const unsigned fields[] = {
      32,       36,       40,
      44,                           // Directory category, offset, size, count.
      135 + 4,  135 + 8,  135 + 12, // Entry kind, offset, length.
      135 + 24, 135 + 28,           // Word array range/length.
  };
  for (unsigned field : fields) {
    Fixture f;
    put(f.bytes + field, 0xffffffff);
    EXPECT_FALSE(LocaleDataView::parse({f.bytes, f.size}).has_value());
  }
  Fixture duplicate;
  put(duplicate.bytes + 135 + 16, 10);
  EXPECT_FALSE(
      LocaleDataView::parse({duplicate.bytes, duplicate.size}).has_value());
  Fixture zero_key;
  put(zero_key.bytes + 135, 0);
  EXPECT_FALSE(
      LocaleDataView::parse({zero_key.bytes, zero_key.size}).has_value());
  Fixture overlap;
  put(overlap.bytes + 135 + 24, 53);
  EXPECT_FALSE(
      LocaleDataView::parse({overlap.bytes, overlap.size}).has_value());
  Fixture directory_overlap;
  put(directory_overlap.bytes + 32 + 16 + 4, 135);
  EXPECT_FALSE(
      LocaleDataView::parse({directory_overlap.bytes, directory_overlap.size})
          .has_value());
  Fixture words;
  put(words.bytes + 135 + 28, 7);
  EXPECT_FALSE(LocaleDataView::parse({words.bytes, words.size}).has_value());
}

TEST(LlvmLibcLocaleDataTest, RejectInvalidStringsAndNames) {
  const unsigned offsets[] = {128, 131, 134, 135 + 48, 135 + 53};
  for (unsigned offset : offsets) {
    Fixture f;
    f.bytes[offset] = (offset == 134 || offset == 135 + 53) ? 'x' : 0;
    EXPECT_FALSE(LocaleDataView::parse({f.bytes, f.size}).has_value());
  }
  Fixture slash;
  slash.bytes[128] = '/';
  EXPECT_FALSE(LocaleDataView::parse({slash.bytes, slash.size}).has_value());
}

TEST(LlvmLibcLocaleDataTest, CrossCategoryReferences) {
  Fixture text;
  text.reference(0, 10, 1);
  auto parsed = LocaleDataView::parse({text.bytes, text.size});
  ASSERT_TRUE(parsed.has_value());
  auto item = parsed->find(5, 30);
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(uint32_t(item->kind), uint32_t(LocaleDataView::Kind::STRING));
  EXPECT_EQ(item->bytes.data(), parsed->find(0, 10)->bytes.data());

  Fixture words;
  words.reference(4, 20, 2);
  auto view = LocaleDataView::parse({words.bytes, words.size});
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->find(5, 30)->word(1).value(), uint32_t(4));
}

TEST(LlvmLibcLocaleDataTest, RejectUnresolvedOrRecursiveReferences) {
  const uint32_t invalid[][3] = {
      {6, 10, 1}, {0, 0, 1},  {0, 11, 1}, // Bad category/key.
      {0, 10, 2}, {0, 10, 0}, {0, 10, 4}, // Bad terminal type.
      {5, 30, 1}, {5, 30, 4},             // Self reference.
  };
  for (const auto &ref : invalid) {
    Fixture f;
    f.reference(ref[0], ref[1], ref[2]);
    EXPECT_FALSE(LocaleDataView::parse({f.bytes, f.size}).has_value());
  }
  Fixture short_ref;
  short_ref.reference(0, 10, 1);
  put(short_ref.bytes + 135 + 5 * 65 + 44, 8);
  EXPECT_FALSE(
      LocaleDataView::parse({short_ref.bytes, short_ref.size}).has_value());
}

TEST(LlvmLibcLocaleDataTest, OwnedCopyAndRetainedView) {
  Fixture f;
  auto copied = LocaleDataFile::copy({f.bytes, f.size});
  ASSERT_TRUE(copied.has_value());
  auto *file = copied.value();
  ASSERT_TRUE(file->retain());
  f.bytes[128] = 'X';
  f.bytes[135 + 48] = 'X';
  file->release();
  EXPECT_STREQ(file->view().name(), "sample");
  EXPECT_EQ(file->view().find(0, 10)->bytes[0], uint8_t('h'));
  file->release();
}

#ifdef LOCALE_DATA_ALLOCATION_TEST
namespace {
bool tracking = false;
int calls = 0, fail_call = 0, live = 0;
void *allocations[4]{};
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++calls == fail_call)
    return nullptr;
  void *p = __real_malloc(size);
  if (tracking && p) {
    for (void *&slot : allocations) {
      if (!slot) {
        slot = p;
        ++live;
        break;
      }
    }
  }
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (p) {
    for (void *&slot : allocations) {
      if (slot == p) {
        slot = nullptr;
        --live;
        break;
      }
    }
  }
  __real_free(p);
}

TEST(LlvmLibcLocaleDataTest, AllocationFailureAndMalformedCopyDoNotLeak) {
  for (int fail = 1; fail <= 2; ++fail) {
    Fixture f;
    calls = live = 0;
    fail_call = fail;
    tracking = true;
    auto copy = LocaleDataFile::copy({f.bytes, f.size});
    tracking = false;
    ASSERT_FALSE(copy.has_value());
    EXPECT_EQ(copy.error(), ENOMEM);
    EXPECT_EQ(live, 0);
    EXPECT_EQ(calls, fail);
  }
  Fixture invalid;
  invalid.bytes[0] = 0;
  calls = live = fail_call = 0;
  tracking = true;
  auto copy = LocaleDataFile::copy({invalid.bytes, invalid.size});
  tracking = false;
  EXPECT_FALSE(copy.has_value());
  EXPECT_EQ(copy.error(), EINVAL);
  EXPECT_EQ(live, 0);
}

TEST(LlvmLibcLocaleDataTest, LastReferenceReleasesStorage) {
  Fixture f;
  calls = live = fail_call = 0;
  tracking = true;
  auto copy = LocaleDataFile::copy({f.bytes, f.size});
  tracking = false;
  ASSERT_TRUE(copy.has_value());
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(live, 2);
  auto *file = copy.value();
  ASSERT_TRUE(file->retain());
  file->release();
  EXPECT_EQ(live, 2);
  EXPECT_STREQ(file->view().name(), "sample");
  file->release();
  EXPECT_EQ(live, 0);
}
#endif
