//===-- Word expansion result composition tests ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/wordexp_macros.h"
#include "src/__support/freelist_heap.h"
#include "src/wordexp/evaluate.h"
#include "src/wordexp/shell_syntax.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::CharVector;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::cpp::string_view;

namespace {
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
int fail_after = -1;
size_t live = 0;
bool fail() { return fail_after >= 0 && fail_after-- == 0; }
struct Fixture {
  wordexp_t result{};
  int lookup_error = 0, command_error = 0;
  unsigned calls = 0, commands = 0;
  ~Fixture() { release_word_results(result); }
  static ErrorOr<WordVariable> lookup(void *p, string_view name) {
    auto &self = *static_cast<Fixture *>(p);
    ++self.calls;
    if (self.lookup_error)
      return Error(self.lookup_error);
    return name == "present" ? WordVariable{true, "one two"}
                             : WordVariable{false, {}};
  }
  static int command(void *p, const WordSource &, WordToken,
                     RestartableConverter, CharVector &out) {
    auto &self = *static_cast<Fixture *>(p);
    ++self.commands;
    if (self.command_error)
      return self.command_error;
    for (char c : string_view("a\0b\n", 4))
      if (!out.append(c))
        return ENOMEM;
    return 0;
  }
  WordEvaluationResult run(string_view text, int flags = 0) {
    WordExpansionServices services;
    services.context = this;
    services.lookup = lookup;
    services.source_command = command;
    services.commands = word_command_syntax();
    return evaluate_wordexp(text, result, flags, RestartableConverter(true),
                            nullptr, nullptr, services);
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

TEST(LlvmLibcWordEvaluationTest, ExpansionFlagsAndResultsCompose) {
  Fixture f;
  f.result.we_offs = 2;
  ASSERT_EQ(f.run("$present '$present' $'\\x41' $((2+3))", WRDE_DOOFFS).status,
            0);
  ASSERT_EQ(f.result.we_wordc, size_t(5));
  EXPECT_EQ(f.result.we_wordv[0], static_cast<char *>(nullptr));
  EXPECT_EQ(f.result.we_wordv[1], static_cast<char *>(nullptr));
  const char *expected[] = {"one", "two", "$present", "A", "5"};
  for (size_t i = 0; i != 5; ++i)
    EXPECT_STREQ(f.result.we_wordv[2 + i], expected[i]);
  auto *first = f.result.we_wordv[2];
  ASSERT_EQ(f.run("\"$(printf value)\"", WRDE_DOOFFS | WRDE_APPEND).status, 0);
  EXPECT_EQ(f.result.we_wordv[2], first);
  EXPECT_STREQ(f.result.we_wordv[7], "ab");
  EXPECT_EQ(f.result.we_wordv[8], static_cast<char *>(nullptr));
  // Input can borrow the result that REUSE will release.
  ASSERT_EQ(f.run(f.result.we_wordv[2], WRDE_REUSE).status, 0);
  EXPECT_EQ(f.result.we_offs, size_t(0));
  ASSERT_EQ(f.result.we_wordc, size_t(1));
  EXPECT_STREQ(f.result.we_wordv[0], "one");
}

TEST(LlvmLibcWordEvaluationTest, PreflightAndAppendFailurePreserveResults) {
  const struct {
    const char *text;
    int flags, status;
  } rows[] = {
      {"$(echo x) |", 0, WRDE_BADCHAR},
      {"$(echo x) '", 0, WRDE_SYNTAX},
      {"${present:-$(echo x)}", WRDE_NOCMD, WRDE_CMDSUB},
      {"$missing", WRDE_UNDEF, WRDE_BADVAL},
  };
  for (auto row : rows) {
    Fixture f;
    ASSERT_EQ(f.run("old").status, 0);
    auto **old = f.result.we_wordv;
    auto *word = old[0];
    f.calls = 0;
    auto result = f.run(row.text, row.flags | WRDE_APPEND);
    EXPECT_EQ(result.status, row.status);
    EXPECT_EQ(result.error, 0);
    EXPECT_EQ(f.commands, 0U);
    if (row.status != WRDE_BADVAL)
      EXPECT_EQ(f.calls, 0U);
    EXPECT_EQ(f.result.we_wordc, size_t(1));
    EXPECT_EQ(f.result.we_wordv, old);
    EXPECT_EQ(old[0], word);
    Fixture fresh;
    EXPECT_EQ(fresh.run(row.text, row.flags).status, row.status);
    EXPECT_EQ(fresh.result.we_wordc, size_t(0));
    EXPECT_EQ(fresh.result.we_wordv, static_cast<char **>(nullptr));
  }
}

TEST(LlvmLibcWordEvaluationTest, ServiceErrorsKeepCauseAndResourcePrefix) {
  const int errors[] = {ENOMEM, EAGAIN, EMFILE, ENFILE, EILSEQ,
                        EIO,    ENOENT, EACCES, ENOTSUP};
  const bool modes[] = {false, true};
  for (int error : errors) {
    bool resource = error == ENOMEM || error == EAGAIN || error == EMFILE ||
                    error == ENFILE;
    int status = resource ? WRDE_NOSPACE : WRDE_SYNTAX;
    for (bool command : modes) {
      Fixture f;
      ASSERT_EQ(f.run("old").status, 0);
      auto **old = f.result.we_wordv;
      if (command)
        f.command_error = error;
      else
        f.lookup_error = error;
      auto result = f.run("before $(echo x)", WRDE_APPEND);
      EXPECT_EQ(result.status, status);
      EXPECT_EQ(result.error, error);
      EXPECT_EQ(f.result.we_wordc, size_t(1 + (resource && command)));
      EXPECT_STREQ(f.result.we_wordv[0], "old");
      if (!resource)
        EXPECT_EQ(f.result.we_wordv, old);
    }
  }
  Fixture invalid;
  auto result = invalid.run("\xff");
  EXPECT_EQ(result.status, WRDE_SYNTAX);
  EXPECT_EQ(result.error, EILSEQ);
  EXPECT_EQ(invalid.calls, 0U);
}

TEST(LlvmLibcWordEvaluationTest, EveryAllocationFailureReleasesPrivateStorage) {
  bool complete = false;
  for (int failure = 0; failure < 512 && !complete; ++failure) {
    size_t before = live;
    {
      Fixture f;
      ASSERT_EQ(f.run("old").status, 0);
      char *old = f.result.we_wordv[0];
      fail_after = failure;
      auto result =
          f.run("one $present ${missing:-$'\\x41'} $(echo x)", WRDE_APPEND);
      fail_after = -1;
      complete = result.status == 0;
      if (!complete) {
        EXPECT_EQ(result.status, WRDE_NOSPACE);
        EXPECT_EQ(result.error, ENOMEM);
      }
      EXPECT_EQ(f.result.we_wordv[0], old);
      EXPECT_EQ(f.result.we_wordv[f.result.we_wordc],
                static_cast<char *>(nullptr));
    }
    EXPECT_EQ(live, before);
  }
  EXPECT_TRUE(complete);
}
