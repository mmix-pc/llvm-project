//===-- Public regex ownership and failure tests
//---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/regex_macros.h"
#include "src/__support/freelist_heap.h"
#include "src/__support/libc_errno.h"
#include "src/ctype/isprint.h"
#include "src/locale/locale_storage.h"
#include "src/locale/uselocale.h"
#include "src/regex/regcomp.h"
#include "src/regex/regerror.h"
#include "src/regex/regexec.h"
#include "src/regex/regfree.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/PatternCollationFixture.h"

using namespace LIBC_NAMESPACE::internal;
namespace {
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
int fail_after = -1;
size_t live = 0, attempts = 0;
bool fail() {
  ++attempts;
  return fail_after >= 0 && fail_after-- == 0;
}
struct Fixture {
  __locale_data *category = nullptr;
  __locale_t locale = locale_from_mask(LOCALE_MASK);
  locale_t previous = nullptr;
  bool initialize() {
    LIBC_NAMESPACE::testing::PatternCollationFixture data;
    if (!data.read())
      return false;
    size_t size = 0;
    for (unsigned i = 0; i != 4; ++i)
      size = (size << 8) | data.bytes[16 + i];
    auto file = LocaleDataFile::copy({data.bytes, size});
    if (!file)
      return false;
    auto record = create_collation_category(**file, true);
    (*file)->release();
    if (!record)
      return false;
    category = *record;
    // Synthetic collation with built-in UTF-8 classification. The two category
    // references deliberately alias to test independently owned references.
    locale.__data[LC_CTYPE] = locale.__data[LC_COLLATE] = category;
    previous = LIBC_NAMESPACE::uselocale(&locale);
    return true;
  }
  ~Fixture() {
    if (category) {
      LIBC_NAMESPACE::uselocale(previous);
      release_locale_category(category);
    }
  }
};
struct Expression {
  regex_t value{};
  ~Expression() {
    if (value.__internal)
      LIBC_NAMESPACE::regfree(&value);
  }
};
} // namespace

extern "C" void *__wrap_malloc(size_t size) {
  if (fail())
    return nullptr;
  void *p = heap.allocate(size);
  live += p != nullptr;
  return p;
}
extern "C" void *__wrap_realloc(void *old, size_t size) {
  if (fail())
    return nullptr;
  void *p = heap.realloc(old, size);
  live += p && !old;
  return p;
}
extern "C" void __wrap_free(void *p) {
  live -= p != nullptr;
  heap.free(p);
}

TEST(LlvmLibcPublicRegexTest, RetainsBothCategoriesUntilRegfree) {
  size_t before = live;
  {
    Fixture fixture;
    ASSERT_TRUE(fixture.initialize());
    Expression re;
    ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re.value, "^([[=a=]])$", REG_EXTENDED),
              0);
    EXPECT_EQ(re.value.re_nsub, size_t(1));
    EXPECT_EQ(fixture.category->references.load(), 3U);
    regmatch_t matches[3];
    ASSERT_EQ(LIBC_NAMESPACE::regexec(&re.value, "ba", 3, matches, 0), 0);
    EXPECT_EQ(matches[1].rm_eo, regoff_t(2));
    EXPECT_EQ(matches[2].rm_so, regoff_t(-1));
    ASSERT_EQ(LIBC_NAMESPACE::regexec(&re.value, "\xc3\xa4", 2, matches, 0), 0);
    EXPECT_EQ(matches[1].rm_eo, regoff_t(2));
    EXPECT_EQ(LIBC_NAMESPACE::regexec(&re.value, "b", 0, nullptr, 0),
              REG_NOMATCH);
    LIBC_NAMESPACE::regfree(&re.value);
    EXPECT_EQ(fixture.category->references.load(), 1U);
  }
  EXPECT_EQ(live, before);
}

TEST(LlvmLibcPublicRegexTest, RetentionOverflowAndInvalidCompilationRollBack) {
  Fixture fixture;
  ASSERT_TRUE(fixture.initialize());
  size_t before = live;
  Expression re;
  EXPECT_EQ(LIBC_NAMESPACE::regcomp(&re.value, "[", 0), REG_EBRACK);
  EXPECT_EQ(fixture.category->references.load(), 1U);
  EXPECT_EQ(live, before);
  fixture.category->references.store(~0U - 1);
  EXPECT_EQ(LIBC_NAMESPACE::regcomp(&re.value, "a", 0), REG_ESPACE);
  EXPECT_EQ(fixture.category->references.load(), ~0U - 1);
  fixture.category->references.store(1);
  EXPECT_EQ(re.value.__internal, static_cast<void *>(nullptr));
  EXPECT_EQ(live, before);
}

TEST(LlvmLibcPublicRegexTest, EveryCompilationAllocationRollsBack) {
  Fixture fixture;
  ASSERT_TRUE(fixture.initialize());
  size_t before = live, count;
  {
    Expression re;
    attempts = 0;
    ASSERT_EQ(
        LIBC_NAMESPACE::regcomp(&re.value, "^([a-b]+)(.)?$", REG_EXTENDED), 0);
    count = attempts;
  }
  EXPECT_EQ(live, before);
  for (size_t at = 0; at != count; ++at) {
    {
      Expression re;
      fail_after = static_cast<int>(at);
      int result =
          LIBC_NAMESPACE::regcomp(&re.value, "^([a-b]+)(.)?$", REG_EXTENDED);
      fail_after = -1;
      EXPECT_TRUE(result == 0 || result == REG_ESPACE);
      if (result)
        EXPECT_EQ(re.value.__internal, static_cast<void *>(nullptr));
    }
    EXPECT_EQ(live, before);
    EXPECT_EQ(fixture.category->references.load(), 1U);
  }
}

TEST(LlvmLibcPublicRegexTest, ExecutionAllocationsAndDecodingPreserveOutputs) {
  Fixture fixture;
  ASSERT_TRUE(fixture.initialize());
  Expression re;
  ASSERT_EQ(
      LIBC_NAMESPACE::regcomp(&re.value, "^([[:alpha:]]+)(.)$", REG_EXTENDED),
      0);
  size_t before = live;
  regmatch_t matches[2];
  attempts = 0;
  ASSERT_EQ(LIBC_NAMESPACE::regexec(&re.value, "\xc3\xa4!", 2, matches, 0), 0);
  size_t count = attempts;
  EXPECT_EQ(matches[1].rm_eo, regoff_t(2));
  for (size_t at = 0; at != count; ++at) {
    matches[0] = {77, 88};
    fail_after = static_cast<int>(at);
    int result = LIBC_NAMESPACE::regexec(&re.value, "\xc3\xa4!", 2, matches, 0);
    fail_after = -1;
    EXPECT_EQ(result, REG_ESPACE);
    EXPECT_EQ(matches[0].rm_so, regoff_t(77));
    EXPECT_EQ(matches[0].rm_eo, regoff_t(88));
    EXPECT_EQ(live, before);
  }
  matches[0] = {77, 88};
  EXPECT_EQ(LIBC_NAMESPACE::regexec(&re.value, "\xc3", 2, matches, 0),
            REG_BADPAT);
  EXPECT_EQ(matches[0].rm_so, regoff_t(77));
  EXPECT_EQ(live, before);
}

TEST(LlvmLibcPublicRegexTest, DefinedInvalidSyntaxDoesNotPublishState) {
  Fixture fixture;
  ASSERT_TRUE(fixture.initialize());
  const struct {
    const char *pattern;
    int flags;
  } cases[] = {{"a\\", 0},
               {"[", 0},
               {"\\(a", 0},
               {"\\1", 0},
               {"a\\{2,1\\}", 0},
               {"[[:unknown:]]", 0},
               {"[[.unknown.]]", 0},
               {"[[=unknown=]]", 0},
               {"a\\", REG_EXTENDED},
               {"[", REG_EXTENDED},
               {"(a", REG_EXTENDED},
               {"a{2,1}", REG_EXTENDED},
               {"*a", REG_EXTENDED}};
  size_t before = live;
  for (const auto &item : cases) {
    Expression re;
    int error = LIBC_NAMESPACE::regcomp(&re.value, item.pattern, item.flags);
    // POSIX permits REG_BADPAT instead of a more specific diagnostic.
    EXPECT_NE(error, 0);
    EXPECT_NE(error, REG_NOMATCH);
    EXPECT_NE(error, REG_ESPACE);
    EXPECT_EQ(re.value.__internal, static_cast<void *>(nullptr));
    EXPECT_EQ(fixture.category->references.load(), 1U);
    EXPECT_EQ(live, before);
  }
}

TEST(LlvmLibcPublicRegexTest, RepeatedNestedCaptureAndLeftmostPriority) {
  Fixture fixture;
  ASSERT_TRUE(fixture.initialize());
  const struct {
    const char *pattern, *input;
    size_t groups;
    regmatch_t expected[4];
  } cases[] = {
      {"(a|aa)(a?)", "xaa", 2, {{1, 3}, {1, 3}, {3, 3}, {-1, -1}}},
      {"(a|ab)(b*)", "ab", 2, {{0, 2}, {0, 2}, {2, 2}, {-1, -1}}},
      {"(a|(b))*", "ba", 2, {{0, 2}, {1, 2}, {-1, -1}, {-1, -1}}},
      {"((a)?b)*", "abb", 2, {{0, 3}, {2, 3}, {-1, -1}, {-1, -1}}},
      {"((a|b)c)+", "acbc", 2, {{0, 4}, {2, 4}, {2, 3}, {-1, -1}}},
      {"(a*)(b*)", "xaaa", 2, {{0, 0}, {0, 0}, {0, 0}, {-1, -1}}},
      {"(a)?()", "b", 2, {{0, 0}, {-1, -1}, {0, 0}, {-1, -1}}},
      {"((a)*){0}b", "b", 2, {{0, 1}, {-1, -1}, {-1, -1}, {-1, -1}}},
  };
  for (const auto &item : cases) {
    Expression re;
    ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re.value, item.pattern, REG_EXTENDED),
              0);
    EXPECT_EQ(re.value.re_nsub, item.groups);
    regmatch_t matches[4];
    ASSERT_EQ(LIBC_NAMESPACE::regexec(&re.value, item.input, 4, matches, 0), 0);
    for (size_t i = 0; i != 4; ++i) {
      EXPECT_EQ(matches[i].rm_so, item.expected[i].rm_so);
      EXPECT_EQ(matches[i].rm_eo, item.expected[i].rm_eo);
    }
  }
}

TEST(LlvmLibcPublicRegexTest, CaptureCapacityAndNoSubexpressions) {
  Fixture fixture;
  ASSERT_TRUE(fixture.initialize());
  const int modes[] = {0, REG_NOSUB};
  for (int flags : modes) {
    Expression re;
    ASSERT_EQ(
        LIBC_NAMESPACE::regcomp(&re.value, "(a)(b)?", REG_EXTENDED | flags), 0);
    for (size_t count = 0; count != 5; ++count) {
      regmatch_t matches[5] = {
          {77, 88}, {77, 88}, {77, 88}, {77, 88}, {77, 88}};
      ASSERT_EQ(LIBC_NAMESPACE::regexec(&re.value, "xa", count, matches, 0), 0);
      for (size_t i = 0; i != 5; ++i) {
        bool untouched = flags == REG_NOSUB || i >= count;
        EXPECT_EQ(matches[i].rm_so, regoff_t(untouched ? 77 : i < 2 ? 1 : -1));
        EXPECT_EQ(matches[i].rm_eo, regoff_t(untouched ? 88 : i < 2 ? 2 : -1));
      }
    }
    EXPECT_EQ(LIBC_NAMESPACE::regexec(&re.value, "xa", 0, nullptr, 0), 0);
  }
  Expression re;
  ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re.value, "^\\(a\\)\\1$", REG_NOSUB), 0);
  EXPECT_EQ(LIBC_NAMESPACE::regexec(&re.value, "aa", 0, nullptr, 0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::regexec(&re.value, "ab", 0, nullptr, 0),
            REG_NOMATCH);
}

TEST(LlvmLibcPublicRegexTest, ErrorTextUsesActualErrorAndEveryCapacity) {
  Fixture fixture;
  ASSERT_TRUE(fixture.initialize());
  const bool failures[] = {false, true};
  for (bool compile_failure : failures) {
    Expression re;
    int error =
        LIBC_NAMESPACE::regcomp(&re.value, compile_failure ? "[" : "a", 0);
    if (!compile_failure) {
      ASSERT_EQ(error, 0);
      error = LIBC_NAMESPACE::regexec(&re.value, "b", 0, nullptr, 0);
    }
    ASSERT_NE(error, 0);
    const regex_t *contexts[] = {&re.value, nullptr};
    for (const regex_t *context : contexts) {
      size_t needed = LIBC_NAMESPACE::regerror(error, context, nullptr, 0);
      ASSERT_GT(needed, size_t(1));
      struct Storage {
        char *full = nullptr, *buffer = nullptr;
        ~Storage() {
          __wrap_free(full);
          __wrap_free(buffer);
        }
      } storage;
      storage.full = static_cast<char *>(__wrap_malloc(needed));
      storage.buffer = static_cast<char *>(__wrap_malloc(needed + 4));
      ASSERT_NE(storage.full, static_cast<char *>(nullptr));
      ASSERT_NE(storage.buffer, static_cast<char *>(nullptr));
      EXPECT_EQ(LIBC_NAMESPACE::regerror(error, context, storage.full, needed),
                needed);
      EXPECT_EQ(storage.full[needed - 1], '\0');
      for (size_t i = 0; i + 1 != needed; ++i)
        EXPECT_NE(LIBC_NAMESPACE::isprint(
                      static_cast<unsigned char>(storage.full[i])),
                  0);
      for (size_t capacity = 0; capacity != needed + 3; ++capacity) {
        for (size_t i = 0; i != needed + 4; ++i)
          storage.buffer[i] = '!';
        EXPECT_EQ(LIBC_NAMESPACE::regerror(error, context, storage.buffer + 1,
                                           capacity),
                  needed);
        EXPECT_EQ(storage.buffer[0], '!');
        EXPECT_EQ(storage.buffer[capacity + 1], '!');
        if (!capacity)
          continue;
        size_t written = capacity < needed ? capacity : needed;
        for (size_t i = 0; i + 1 != written; ++i)
          EXPECT_EQ(storage.buffer[i + 1], storage.full[i]);
        EXPECT_EQ(storage.buffer[written], '\0');
      }
    }
  }
}

TEST(LlvmLibcPublicRegexTest, RequiredPatternLengthAndRegfreeState) {
  Fixture fixture;
  ASSERT_TRUE(fixture.initialize());
  char pattern[257], input[258];
  for (size_t i = 0; i != 256; ++i)
    pattern[i] = input[i + 1] = static_cast<char>('a' + i % 20);
  input[0] = 'z';
  pattern[256] = input[257] = 0;
  size_t before = live;
  const int modes[] = {0, REG_EXTENDED};
  for (int mode : modes) {
    Expression re;
    ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re.value, pattern, mode), 0);
    regmatch_t match;
    ASSERT_EQ(LIBC_NAMESPACE::regexec(&re.value, input, 1, &match, 0), 0);
    EXPECT_EQ(match.rm_so, regoff_t(1));
    EXPECT_EQ(match.rm_eo, regoff_t(257));
    libc_errno = 123;
    LIBC_NAMESPACE::regfree(&re.value);
    EXPECT_EQ(int(libc_errno), 123);
    EXPECT_EQ(live, before);
    EXPECT_EQ(fixture.category->references.load(), 1U);
  }
}
