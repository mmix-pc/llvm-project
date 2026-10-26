//===-- Locale object ownership tests -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/app.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/File/file.h"
#include "src/__support/libc_errno.h"
#include "src/locale/duplocale.h"
#include "src/locale/freelocale.h"
#include "src/locale/locale_storage.h"
#include "src/locale/newlocale.h"
#include "src/locale/uselocale.h"
#include "test/UnitTest/Test.h"

namespace LIBC_NAMESPACE_DECL {
AppProperties app;
}

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::cpp::MemoryOrder;

namespace {
// Only the lifetime factory sees this empty-category container. No public
// locale resolver accepts it as regional data or claims semantic validation.
struct Fixture {
  uint8_t bytes[135]{};
  static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - i * 8));
  }
  Fixture() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 16, sizeof(bytes));
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 7);
    const char name[] = "sample";
    for (unsigned i = 0; i != sizeof(name); ++i)
      bytes[128 + i] = uint8_t(name[i]);
    for (unsigned i = 0; i != 6; ++i) {
      put(bytes + 32 + 16 * i, i);
      put(bytes + 36 + 16 * i, sizeof(bytes));
    }
  }
  LIBC_NAMESPACE::ErrorOr<__locale_data *> category() {
    auto file = LocaleDataFile::copy(bytes);
    if (!file)
      return LIBC_NAMESPACE::Error(file.error());
    auto result = create_locale_category(**file, true);
    (*file)->release();
    return result;
  }
};
} // namespace

namespace {
class EncodingFile : public LIBC_NAMESPACE::File {
  static int finish(File *) { return 0; }

public:
  EncodingFile()
      : File(nullptr, nullptr, nullptr, finish, nullptr, 0, _IONBF, false,
             ModeFlags(OpenMode::READ)) {}
  void reopen() { reset_stream_state_unlocked(ModeFlags(OpenMode::READ)); }
};
} // namespace

TEST(LlvmLibcLocaleOwnershipTest, WideStreamOwnsCategoryUntilReopenOrClose) {
  Fixture fixture;
  auto first = fixture.category(), second = fixture.category();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  auto object = locale_from_mask(0);
  object.data[LC_CTYPE] = *first;
  auto previous = thread_locale;
  thread_locale = &object;
  EncodingFile file;
  using Orientation = LIBC_NAMESPACE::File::Orientation;
  EXPECT_EQ(file.try_set_orientation(Orientation::WIDE), Orientation::WIDE);
  EXPECT_EQ((*first)->references.load(), 2U);
  object.data[LC_CTYPE] = *second;
  EXPECT_EQ(file.try_set_orientation(Orientation::WIDE), Orientation::WIDE);
  EXPECT_EQ((*first)->references.load(), 2U);
  EXPECT_EQ((*second)->references.load(), 1U);
  file.reopen();
  EXPECT_EQ((*first)->references.load(), 1U);
  EXPECT_EQ(file.try_set_orientation(Orientation::WIDE), Orientation::WIDE);
  EXPECT_EQ((*second)->references.load(), 2U);
  thread_locale = previous;
  EXPECT_EQ(file.close(), 0);
  EXPECT_EQ((*second)->references.load(), 1U);
  release_locale_category(*first);
  release_locale_category(*second);
}

TEST(LlvmLibcLocaleOwnershipTest, FailedStreamCaptureDoesNotSetOrientation) {
  Fixture fixture;
  auto category = fixture.category();
  ASSERT_TRUE(category.has_value());
  auto object = locale_from_mask(0);
  object.data[LC_CTYPE] = *category;
  auto previous = thread_locale;
  thread_locale = &object;
  (*category)->references.store(~0U, MemoryOrder::RELAXED);
  EncodingFile file;
  using Orientation = LIBC_NAMESPACE::File::Orientation;
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(file.try_set_orientation(Orientation::WIDE),
            Orientation::UNORIENTED);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
  EXPECT_EQ((*category)->references.load(), ~0U);
  EXPECT_EQ(file.try_set_orientation(Orientation::BYTE), Orientation::BYTE);
  EXPECT_EQ(file.close(), 0);
  EXPECT_EQ((*category)->references.load(), ~0U);
  (*category)->references.store(1, MemoryOrder::RELAXED);
  thread_locale = previous;
  release_locale_category(*category);
}

TEST(LlvmLibcLocaleOwnershipTest, DuplicateRetainsIndependentObject) {
  Fixture f;
  auto category = f.category();
  ASSERT_TRUE(category.has_value());
  locale_t object = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C", nullptr);
  ASSERT_NE(object, nullptr);
  object->data[LC_CTYPE] = *category;
  auto copy = LIBC_NAMESPACE::duplocale(object);
  ASSERT_NE(copy, nullptr);
  EXPECT_NE(copy, object);
  EXPECT_EQ((*category)->references.load(), 2U);
  LIBC_NAMESPACE::freelocale(object);
  EXPECT_STREQ(copy->data[LC_CTYPE]->name, "sample");
  EXPECT_EQ(copy->data[LC_CTYPE]->references.load(), 1U);
  EXPECT_STREQ(copy->data[LC_CTYPE]->storage->view().name(), "sample");
  LIBC_NAMESPACE::freelocale(copy);
}

TEST(LlvmLibcLocaleOwnershipTest, ReplacementPreservesUnselectedReferences) {
  Fixture f;
  auto category = f.category();
  ASSERT_TRUE(category.has_value());
  locale_t base = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C", nullptr);
  ASSERT_NE(base, nullptr);
  base->data[LC_CTYPE] = *category;
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(LC_NUMERIC_MASK, "C.UTF-8", base), base);
  EXPECT_EQ(base->data[LC_CTYPE], *category);
  EXPECT_EQ((*category)->references.load(), 1U);
  EXPECT_TRUE(base->data[LC_NUMERIC]->utf8);
  auto old = base->data[LC_NUMERIC];
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "missing", base), nullptr);
  EXPECT_EQ(base->data[LC_NUMERIC], old);
  EXPECT_EQ(base->data[LC_CTYPE], *category);
  EXPECT_EQ((*category)->references.load(), 1U);
  LIBC_NAMESPACE::freelocale(base);
}

TEST(LlvmLibcLocaleOwnershipTest, RepeatedCategoryAndBorrowedThreadSelection) {
  Fixture f;
  auto category = f.category();
  ASSERT_TRUE(category.has_value());
  locale_t object = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C", nullptr);
  ASSERT_NE(object, nullptr);
  object->data[LC_CTYPE] = *category;
  ASSERT_TRUE(retain_locale_category(*category));
  object->data[LC_NUMERIC] = *category;
  auto previous = LIBC_NAMESPACE::uselocale(object);
  EXPECT_EQ((*category)->references.load(), 2U);
  EXPECT_EQ(LIBC_NAMESPACE::uselocale(nullptr), object);
  LIBC_NAMESPACE::uselocale(previous);
  EXPECT_EQ((*category)->references.load(), 2U);
  auto copy = LIBC_NAMESPACE::duplocale(object);
  ASSERT_NE(copy, nullptr);
  EXPECT_EQ((*category)->references.load(), 4U);
  LIBC_NAMESPACE::freelocale(copy);
  EXPECT_EQ((*category)->references.load(), 2U);
  LIBC_NAMESPACE::freelocale(object);
}

TEST(LlvmLibcLocaleOwnershipTest, SaturatedReferenceRollsBackPrefix) {
  Fixture f;
  auto a = f.category(), b = f.category();
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  auto object = locale_from_mask(0);
  object.data[LC_CTYPE] = *a;
  object.data[LC_NUMERIC] = *b;
  (*b)->references.store(~0U, MemoryOrder::RELAXED);
  EXPECT_FALSE(retain_locale(object));
  EXPECT_EQ((*a)->references.load(), 1U);
  EXPECT_EQ((*b)->references.load(), ~0U);
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::duplocale(&object), nullptr);
  EXPECT_EQ(static_cast<int>(LIBC_NAMESPACE::libc_errno), ENOMEM);
  EXPECT_EQ((*a)->references.load(), 1U);
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(LC_TIME_MASK, "C", &object), nullptr);
  EXPECT_EQ(object.data[LC_CTYPE], *a);
  EXPECT_EQ(object.data[LC_NUMERIC], *b);
  EXPECT_EQ((*a)->references.load(), 1U);
  (*b)->references.store(1, MemoryOrder::RELAXED);
  release_locale(object);
}

TEST(LlvmLibcLocaleOwnershipTest, NamesAreNotEncodingIdentity) {
  __locale_data first("en_US.UTF-8", true), second("tr_TR.UTF-8", true);
  __locale_t object;
  for (auto *&entry : object.data)
    entry = &first;
  EXPECT_STREQ(locale_name(object, LC_ALL), "en_US.UTF-8");
  object.data[LC_NUMERIC] = &second;
  EXPECT_STREQ(locale_name(object, LC_ALL),
               "LC_CTYPE=en_US.UTF-8;LC_NUMERIC=tr_TR.UTF-8;"
               "LC_TIME=en_US.UTF-8;LC_COLLATE=en_US.UTF-8;"
               "LC_MONETARY=en_US.UTF-8;LC_MESSAGES=en_US.UTF-8");
  EXPECT_STREQ(locale_name(object, LC_NUMERIC), "tr_TR.UTF-8");
}

TEST(LlvmLibcLocaleOwnershipTest, GlobalPublicationRetainsCategories) {
  Fixture f;
  auto category = f.category();
  ASSERT_TRUE(category.has_value());
  auto previous = global_locale_snapshot();
  ASSERT_TRUE(retain_locale(previous));
  auto next = locale_from_mask(0);
  next.data[LC_CTYPE] = next.data[LC_NUMERIC] = *category;
  EXPECT_TRUE(publish_global_locale(next));
  EXPECT_EQ((*category)->references.load(), 3U);
  release_locale_category(*category);
  EXPECT_EQ(global_locale_snapshot().data[LC_CTYPE], *category);
  auto duplicate = LIBC_NAMESPACE::duplocale(LC_GLOBAL_LOCALE);
  EXPECT_NE(duplicate, nullptr);
  EXPECT_TRUE(publish_global_locale(previous));
  release_locale(previous);
  if (duplicate) {
    EXPECT_STREQ(duplicate->data[LC_CTYPE]->name, "sample");
    EXPECT_EQ(duplicate->data[LC_CTYPE]->references.load(), 2U);
    LIBC_NAMESPACE::freelocale(duplicate);
  }
}

TEST(LlvmLibcLocaleOwnershipTest, GlobalPublicationFailurePreservesState) {
  Fixture f;
  auto first = f.category(), second = f.category();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  auto previous = global_locale_snapshot();
  auto next = previous;
  next.data[LC_CTYPE] = *first;
  next.data[LC_NUMERIC] = *second;
  (*second)->references.store(~0U, MemoryOrder::RELAXED);
  EXPECT_FALSE(publish_global_locale(next));
  for (int i = 0; i != LOCALE_CATEGORY_COUNT; ++i)
    EXPECT_EQ(global_locale_snapshot().data[i], previous.data[i]);
  EXPECT_EQ((*first)->references.load(), 1U);
  (*second)->references.store(1, MemoryOrder::RELAXED);
  release_locale_category(*first);
  release_locale_category(*second);
}

TEST(LlvmLibcLocaleOwnershipTest, MaximumLengthCompositeName) {
  char names[LOCALE_CATEGORY_COUNT][256];
  for (unsigned i = 0; i != LOCALE_CATEGORY_COUNT; ++i) {
    for (unsigned j = 0; j != 255; ++j)
      names[i][j] = char('a' + i);
    names[i][255] = '\0';
  }
  __locale_data records[] = {{names[0], true}, {names[1], true},
                             {names[2], true}, {names[3], true},
                             {names[4], true}, {names[5], true}};
  __locale_t object;
  for (unsigned i = 0; i != LOCALE_CATEGORY_COUNT; ++i)
    object.data[i] = &records[i];
  LIBC_NAMESPACE::cpp::string_view result(locale_name(object, LC_ALL));
  for (unsigned i = 0; i != LOCALE_CATEGORY_COUNT; ++i) {
    LIBC_NAMESPACE::cpp::string_view key(category_names[i]);
    ASSERT_TRUE(result.starts_with(key));
    result.remove_prefix(key.size());
    ASSERT_TRUE(result.starts_with("="));
    result.remove_prefix(1);
    ASSERT_TRUE(result.starts_with(names[i]));
    result.remove_prefix(255);
    if (i + 1 != LOCALE_CATEGORY_COUNT) {
      ASSERT_TRUE(result.starts_with(";"));
      result.remove_prefix(1);
    }
  }
  EXPECT_TRUE(result.empty());
}

#ifdef LOCALE_OWNERSHIP_ALLOCATION_TEST
namespace {
bool tracking = false;
int calls = 0, fail_call = 0, live = 0;
void *allocations[16]{};
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++calls == fail_call)
    return nullptr;
  void *p = __real_malloc(size);
  if (tracking && p)
    for (void *&slot : allocations)
      if (!slot) {
        slot = p;
        ++live;
        break;
      }
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (p)
    for (void *&slot : allocations)
      if (slot == p) {
        slot = nullptr;
        --live;
        break;
      }
  __real_free(p);
}

TEST(LlvmLibcLocaleOwnershipTest, AllocationFailureAndLastRelease) {
  Fixture f;
  for (int failure = 1; failure <= 3; ++failure) {
    calls = 0;
    fail_call = failure;
    tracking = true;
    auto category = f.category();
    tracking = false;
    EXPECT_FALSE(category.has_value());
    EXPECT_EQ(category.error(), ENOMEM);
    EXPECT_EQ(live, 0);
  }
  calls = fail_call = 0;
  tracking = true;
  auto category = f.category();
  auto object = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C", nullptr);
  tracking = false;
  ASSERT_TRUE(category.has_value());
  ASSERT_NE(object, nullptr);
  object->data[LC_CTYPE] = *category;
  EXPECT_EQ(live, 4);
  calls = 0;
  fail_call = 1;
  tracking = true;
  auto failed = LIBC_NAMESPACE::duplocale(object);
  tracking = false;
  EXPECT_EQ(failed, nullptr);
  EXPECT_EQ((*category)->references.load(), 1U);
  EXPECT_EQ(live, 4);
  calls = fail_call = 0;
  tracking = true;
  auto copy = LIBC_NAMESPACE::duplocale(object);
  tracking = false;
  ASSERT_NE(copy, nullptr);
  EXPECT_EQ(live, 5);
  EXPECT_EQ(LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C", object), object);
  EXPECT_EQ(live, 5);
  LIBC_NAMESPACE::freelocale(copy);
  EXPECT_EQ(live, 1);
  LIBC_NAMESPACE::freelocale(object);
  EXPECT_EQ(live, 0);
}
#endif
