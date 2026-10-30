//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unit tests for FlatFileDatabase.
///
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/CPP/span.h"
#include "src/__support/File/file.h"
#include "src/pwd/field_tokenizer.h"
#include "src/pwd/flat_file_db.h"
#include "src/stdio/remove.h"
#include "src/string/string_utils.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

namespace {

struct SimpleTestEntry {
  const char *key;
  const char *val;
};

class HermeticFile {
  char path[256];

public:
  HermeticFile(const char *file_path, const char *content, size_t len) {
    LIBC_NAMESPACE::internal::strlcpy(path, file_path, sizeof(path));

    auto file_or = LIBC_NAMESPACE::openfile(path, "w");
    if (file_or.has_value()) {
      auto *f = file_or.value();
      auto written = f->write(content, len);
      EXPECT_EQ(written.value, len);
      EXPECT_EQ(written.error, 0);
      EXPECT_EQ(f->close(), 0);
    }
  }

  HermeticFile(const char *file_path, const char *content)
      : HermeticFile(file_path, content,
                     LIBC_NAMESPACE::internal::string_length(content)) {}

  ~HermeticFile() { LIBC_NAMESPACE::remove(path); }

  const char *get_path() const { return path; }
};

class LlvmLibcFlatFileDbTest
    : public LIBC_NAMESPACE::testing::ErrnoCheckingTest {};

} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace pwd {

template <>
inline bool parse_line<SimpleTestEntry>(cpp::span<char> line,
                                        SimpleTestEntry *entry) {
  if (line.empty() || !entry)
    return false;
  FieldTokenizer tokenizer(line);
  auto k = tokenizer.next_field();
  if (!k)
    return false;
  entry->key = k->data();

  auto v = tokenizer.next_field();
  if (!v)
    return false;
  entry->val = v->data();

  return true;
}

} // namespace pwd
} // namespace LIBC_NAMESPACE_DECL

TEST_F(LlvmLibcFlatFileDbTest, GetNextAndLookup) {
  const char *content = "user1:secret1\nuser2:secret2\n";
  HermeticFile test_file(libc_make_test_file_path("flat_db_test.test"),
                         content);

  LIBC_NAMESPACE::pwd::FlatFileDatabase<SimpleTestEntry> db(
      test_file.get_path());
  char buffer[128];
  SimpleTestEntry entry;

  // First record
  auto r1 = db.getnext(&entry, buffer);
  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r1.value());
  ASSERT_STREQ(entry.key, "user1");
  ASSERT_STREQ(entry.val, "secret1");

  // Second record
  auto r2 = db.getnext(&entry, buffer);
  ASSERT_TRUE(r2.has_value());
  ASSERT_TRUE(r2.value());
  ASSERT_STREQ(entry.key, "user2");
  ASSERT_STREQ(entry.val, "secret2");

  // EOF
  auto r3 = db.getnext(&entry, buffer);
  ASSERT_TRUE(r3.has_value());
  ASSERT_FALSE(r3.value());

  // Rewind and lookup
  db.setdb();
  auto matcher = [](const SimpleTestEntry &e) {
    return LIBC_NAMESPACE::cpp::string_view(e.key) == "user2";
  };
  auto lookup_res = db.lookup(matcher, &entry, buffer);
  ASSERT_TRUE(lookup_res.has_value());
  ASSERT_TRUE(lookup_res.value());
  ASSERT_STREQ(entry.key, "user2");
  ASSERT_STREQ(entry.val, "secret2");

  db.enddb();
}

TEST_F(LlvmLibcFlatFileDbTest, LookupNotFound) {
  const char *content = "foo:bar\n";
  HermeticFile test_file(libc_make_test_file_path("flat_db_not_found.test"),
                         content);

  LIBC_NAMESPACE::pwd::FlatFileDatabase<SimpleTestEntry> db(
      test_file.get_path());
  char buffer[128];
  SimpleTestEntry entry;

  auto matcher = [](const SimpleTestEntry &e) {
    return LIBC_NAMESPACE::cpp::string_view(e.key) == "nonexistent";
  };
  auto lookup_res = db.lookup(matcher, &entry, buffer);
  ASSERT_TRUE(lookup_res.has_value());
  ASSERT_FALSE(lookup_res.value());

  db.enddb();
}

TEST_F(LlvmLibcFlatFileDbTest, TruncatedLineReturnsErange) {
  const char *content = "verylongkeyname:verylongvaluename\n";
  HermeticFile test_file(libc_make_test_file_path("flat_db_trunc.test"),
                         content);

  LIBC_NAMESPACE::pwd::FlatFileDatabase<SimpleTestEntry> db(
      test_file.get_path());
  char small_buffer[8];
  SimpleTestEntry entry;

  auto res = db.getnext(&entry, small_buffer);
  ASSERT_FALSE(res.has_value());
  ASSERT_EQ(res.error(), ERANGE);

  db.enddb();
}

TEST_F(LlvmLibcFlatFileDbTest, MalformedLineReturnsEinval) {
  const char *content = "invalid_line_without_delimiter\n";
  HermeticFile test_file(libc_make_test_file_path("flat_db_malformed.test"),
                         content);

  LIBC_NAMESPACE::pwd::FlatFileDatabase<SimpleTestEntry> db(
      test_file.get_path());
  char buffer[128];
  SimpleTestEntry entry;

  auto res = db.getnext(&entry, buffer);
  ASSERT_FALSE(res.has_value());
  ASSERT_EQ(res.error(), EINVAL);

  db.enddb();
}

TEST_F(LlvmLibcFlatFileDbTest, BlankLineIsNotEof) {
  HermeticFile test_file(libc_make_test_file_path("flat_db_blank.test"),
                         "\nabc:def\n");
  LIBC_NAMESPACE::pwd::FlatFileDatabase<SimpleTestEntry> db(
      test_file.get_path());
  SimpleTestEntry entry;
  char buffer[16];
  auto blank = db.getnext(&entry, buffer);
  ASSERT_FALSE(blank.has_value());
  EXPECT_EQ(blank.error(), EINVAL);
  auto valid = db.getnext(&entry, buffer);
  ASSERT_TRUE(valid.has_value());
  ASSERT_TRUE(valid.value());
  EXPECT_STREQ(entry.key, "abc");
  auto eof = db.getnext(&entry, buffer);
  ASSERT_TRUE(eof.has_value());
  EXPECT_FALSE(eof.value());
  db.enddb();
}

TEST_F(LlvmLibcFlatFileDbTest, ExactFitWithAndWithoutNewline) {
  constexpr char records[] = "abc:def\nxyz:uvw";
  HermeticFile test_file(libc_make_test_file_path("flat_db_exact.test"),
                         records, sizeof(records) - 1);
  auto opened = LIBC_NAMESPACE::openfile(test_file.get_path(), "r");
  ASSERT_TRUE(opened.has_value());
  char contents[16] = {};
  auto read = opened.value()->read(contents, 15);
  EXPECT_EQ(read.value, size_t(15));
  EXPECT_STREQ(contents, "abc:def\nxyz:uvw");
  EXPECT_EQ(opened.value()->close(), 0);
  LIBC_NAMESPACE::pwd::FlatFileDatabase<SimpleTestEntry> db(
      test_file.get_path());
  SimpleTestEntry entry;
  char buffer[10] = {'L', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', 'R'};
  auto span = LIBC_NAMESPACE::cpp::span<char>(buffer + 1, 8);
  auto first = db.getnext(&entry, span);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(first.value());
  EXPECT_STREQ(entry.key, "abc");
  EXPECT_STREQ(entry.val, "def");
  auto last = db.getnext(&entry, span);
  ASSERT_TRUE(last.has_value());
  ASSERT_TRUE(last.value());
  EXPECT_STREQ(entry.key, "xyz");
  EXPECT_STREQ(entry.val, "uvw");
  EXPECT_EQ(buffer[0], 'L');
  EXPECT_EQ(buffer[9], 'R');
  auto eof = db.getnext(&entry, span);
  ASSERT_TRUE(eof.has_value());
  EXPECT_FALSE(eof.value());
  db.enddb();
}

TEST_F(LlvmLibcFlatFileDbTest, OverflowStillReturnsErangeAndConsumesLine) {
  HermeticFile test_file(libc_make_test_file_path("flat_db_overflow.test"),
                         "abc:defg\nxyz:uvw\n");
  LIBC_NAMESPACE::pwd::FlatFileDatabase<SimpleTestEntry> db(
      test_file.get_path());
  SimpleTestEntry entry;
  char buffer[8];
  auto overflow = db.getnext(&entry, buffer);
  ASSERT_FALSE(overflow.has_value());
  EXPECT_EQ(overflow.error(), ERANGE);
  auto valid = db.getnext(&entry, buffer);
  ASSERT_TRUE(valid.has_value());
  ASSERT_TRUE(valid.value());
  EXPECT_STREQ(entry.key, "xyz");
  EXPECT_STREQ(entry.val, "uvw");
  db.enddb();
}

TEST_F(LlvmLibcFlatFileDbTest, InvalidBuffersDoNotConsumeRecord) {
  HermeticFile test_file(libc_make_test_file_path("flat_db_buffer.test"),
                         "abc:def\n");
  LIBC_NAMESPACE::pwd::FlatFileDatabase<SimpleTestEntry> db(
      test_file.get_path());
  SimpleTestEntry entry;
  char buffer[8];
  for (size_t size = 0; size < 2; ++size) {
    auto invalid =
        db.getnext(&entry, LIBC_NAMESPACE::cpp::span<char>(buffer, size));
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error(), EINVAL);
  }
  auto valid = db.getnext(&entry, buffer);
  ASSERT_TRUE(valid.has_value());
  ASSERT_TRUE(valid.value());
  EXPECT_STREQ(entry.key, "abc");
  db.enddb();
}
