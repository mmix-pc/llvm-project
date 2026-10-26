//===-- Word expansion composition tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/wordexp/expand.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::cpp::span;
using LIBC_NAMESPACE::cpp::string_view;
using Failure = WordExpansionError;

namespace {
struct Fixture {
  struct Variable {
    const char *name;
    char value[256];
    bool set;
  } vars[4] = {{"x", "", false},
               {"y", "", false},
               {"present", "original", true},
               {"empty", "", true}};
  uint32_t source[2048];
  size_t length = 0;
  unsigned reads = 0, writes = 0, diagnostics = 0;
  int assign_error = 0, sink_error = 0;
  char words[32][256]{};
  size_t sizes[32]{}, count = 0;
  long number = 5;
  char scratch[256]{};

  static ErrorOr<WordVariable> lookup(void *p, string_view name) {
    auto &self = *static_cast<Fixture *>(p);
    ++self.reads;
    for (auto &v : self.vars)
      if (name == v.name) {
        size_t i = 0;
        do {
          self.scratch[i] = v.value[i];
        } while (v.value[i++]);
        return WordVariable{v.set, self.scratch};
      }
    return WordVariable{false, {}};
  }
  static int assign(void *p, string_view name, string_view value) {
    auto &self = *static_cast<Fixture *>(p);
    ++self.writes;
    if (self.assign_error)
      return self.assign_error;
    for (auto &v : self.vars)
      if (name == v.name) {
        if (value.size() >= sizeof(v.value))
          return ENOMEM;
        for (size_t i = 0; i < value.size(); ++i)
          v.value[i] = value[i];
        v.value[value.size()] = 0;
        v.set = true;
        return 0;
      }
    return EINVAL;
  }
  static ErrorOr<WordVariable> home(void *, string_view user) {
    return user.empty()     ? WordVariable{true, "/home/a b"}
           : user == "user" ? WordVariable{true, "/users/*"}
                            : WordVariable{false, {}};
  }
  static int diagnostic(void *p, string_view, string_view) {
    ++static_cast<Fixture *>(p)->diagnostics;
    return 0;
  }
  static ErrorOr<long> read_number(void *p, string_view) {
    return static_cast<Fixture *>(p)->number;
  }
  static int write_number(void *p, string_view, long value) {
    static_cast<Fixture *>(p)->number = value;
    return 0;
  }
  static int append(void *p, string_view value, bool) {
    auto &self = *static_cast<Fixture *>(p);
    if (self.sink_error)
      return self.sink_error;
    if (self.count == 32 || value.size() >= 256 - self.sizes[self.count])
      return ENOMEM;
    for (char c : value)
      self.words[self.count][self.sizes[self.count]++] = c;
    self.words[self.count][self.sizes[self.count]] = 0;
    return 0;
  }
  static int finish(void *p) {
    ++static_cast<Fixture *>(p)->count;
    return 0;
  }
  static int word(void *p, span<const ExpansionPart> parts) {
    auto fields =
        split_word_fields(parts, DEFAULT_FIELD_DELIMITERS, {p, append, finish});
    return fields ? 0 : fields.error();
  }
  WordExpansionResult run(const char *text, bool undef = false) {
    length = 0;
    while (*text)
      source[length++] = static_cast<unsigned char>(*text++);
    return run_scalars(undef);
  }
  WordExpansionResult run_scalars(bool undef = false) {
    WordExpansionServices services;
    services.context = this;
    services.lookup = lookup;
    services.assign = assign;
    services.home = home;
    services.diagnostic = diagnostic;
    services.arithmetic = {this, read_number, write_number};
    return expand_word_fragments({source, length}, true, undef,
                                 RestartableConverter(true), nullptr, nullptr,
                                 services, {this, word});
  }
};
} // namespace

TEST(LlvmLibcWordExpandTest, LazyAlternativesAndAssignments) {
  Fixture f;
  ASSERT_TRUE(bool(f.run("${present:-${x:=bad}} ${x:=new} $x ${empty-word} "
                         "${empty:-default} ${missing+${y:=bad}}")));
  ASSERT_EQ(f.count, size_t(4));
  EXPECT_STREQ(f.words[0], "original");
  EXPECT_STREQ(f.words[1], "new");
  EXPECT_STREQ(f.words[2], "new");
  EXPECT_STREQ(f.words[3], "default");
  EXPECT_EQ(f.writes, 1U);
  EXPECT_FALSE(f.vars[1].set);
}

TEST(LlvmLibcWordExpandTest, OriginsQuotingAndEmptyWords) {
  Fixture f;
  ASSERT_EQ(Fixture::assign(&f, "x", "a b"), 0);
  ASSERT_TRUE(bool(f.run("pre${x}post \"$x\" ${y} \"$y\" '' "
                         "${y:-'c d'} ${y:-e f}")));
  ASSERT_EQ(f.count, size_t(8));
  EXPECT_STREQ(f.words[0], "prea");
  EXPECT_STREQ(f.words[1], "bpost");
  EXPECT_STREQ(f.words[2], "a b");
  EXPECT_STREQ(f.words[3], "");
  EXPECT_STREQ(f.words[4], "");
  EXPECT_STREQ(f.words[5], "c d");
  EXPECT_STREQ(f.words[6], "e");
  EXPECT_STREQ(f.words[7], "f");
}

TEST(LlvmLibcWordExpandTest, PatternRemovalAndCharacterLengths) {
  Fixture f;
  ASSERT_EQ(Fixture::assign(&f, "x", "abac"), 0);
  ASSERT_EQ(Fixture::assign(&f, "y", "\xe4\xb8\xad\xc4\xb1x"), 0);
  ASSERT_TRUE(bool(f.run("${x#a*} \"${x##a*}\" ${x%*c} \"${x%%*c}\" "
                         "${x#'a*'} ${#y} ${y#?} ${y%??}")));
  ASSERT_EQ(f.count, size_t(8));
  EXPECT_STREQ(f.words[0], "bac");
  EXPECT_STREQ(f.words[1], "");
  EXPECT_STREQ(f.words[2], "aba");
  EXPECT_STREQ(f.words[3], "");
  EXPECT_STREQ(f.words[4], "abac");
  EXPECT_STREQ(f.words[5], "3");
  EXPECT_STREQ(f.words[6], "\xc4\xb1x");
  EXPECT_STREQ(f.words[7], "\xe4\xb8\xad");
}

TEST(LlvmLibcWordExpandTest, ArithmeticTildeAndDollarQuotes) {
  Fixture f;
  ASSERT_TRUE(bool(f.run("$((2+${x:-3}*(1+2))) $((n+=2)) ~ ~user/end "
                         "~unknown $'a\\0discarded'b $'\\x41\\101' "
                         "\"${x:-\"quoted word\"}\"")));
  ASSERT_EQ(f.count, size_t(8));
  EXPECT_STREQ(f.words[0], "11");
  EXPECT_STREQ(f.words[1], "7");
  EXPECT_EQ(f.number, 7L);
  EXPECT_STREQ(f.words[2], "/home/a b");
  EXPECT_STREQ(f.words[3], "/users/*/end");
  EXPECT_STREQ(f.words[4], "~unknown");
  EXPECT_STREQ(f.words[5], "ab");
  EXPECT_STREQ(f.words[6], "AA");
  EXPECT_STREQ(f.words[7], "quoted word");
}

TEST(LlvmLibcWordExpandTest, FullPreflightBeforeServices) {
  const char *bad[] = {"${x=changed} $'\\q'", "${x=changed} unmatched'",
                       "${x=changed} ${present:-$(echo forbidden)}",
                       "${x=changed} a|b"};
  for (auto text : bad) {
    Fixture f;
    EXPECT_FALSE(bool(f.run(text)));
    EXPECT_EQ(f.reads, 0U);
    EXPECT_EQ(f.writes, 0U);
    EXPECT_EQ(f.count, size_t(0));
  }
}

TEST(LlvmLibcWordExpandTest, UndefinedSelectedErrorsAndSinkFailures) {
  Fixture missing;
  EXPECT_TRUE(missing.run("$x", true).error == Failure::UNDEFINED);
  Fixture defaults;
  ASSERT_TRUE(bool(defaults.run("${x:-fallback} ${x+unused}", true)));
  EXPECT_EQ(defaults.count, size_t(1));
  Fixture error;
  EXPECT_TRUE(error.run("${x:?${y:=message}} tail").error ==
              Failure::PARAMETER_ERROR);
  EXPECT_EQ(error.diagnostics, 1U);
  EXPECT_EQ(error.writes, 1U);
  EXPECT_EQ(error.count, size_t(0));
  Fixture allocation;
  allocation.assign_error = ENOMEM;
  auto result = allocation.run("${x:=value}");
  EXPECT_TRUE(result.error == Failure::SYSTEM);
  EXPECT_EQ(result.system_error, ENOMEM);
  EXPECT_FALSE(allocation.vars[0].set);
  Fixture sink;
  sink.sink_error = EIO;
  result = sink.run("one two");
  EXPECT_TRUE(result.error == Failure::SYSTEM);
  EXPECT_EQ(result.system_error, EIO);
}

TEST(LlvmLibcWordExpandTest, PriorValuesAreOwnedAcrossNestedAssignments) {
  Fixture f;
  ASSERT_EQ(Fixture::assign(&f, "x", "a b"), 0);
  // The first value must not borrow x's storage when a later expansion changes
  // it before the complete source word is passed to field splitting.
  ASSERT_TRUE(bool(f.run("$x${y:=${x:=unused}} ${x:=${y:=unused}}")));
  EXPECT_STREQ(f.words[0], "a");
  EXPECT_STREQ(f.words[1], "ba");
  EXPECT_STREQ(f.words[2], "b");
  Fixture pattern;
  ASSERT_EQ(Fixture::assign(&pattern, "x", "abc"), 0);
  ASSERT_TRUE(bool(pattern.run("${x#${y:=a}}")));
  EXPECT_STREQ(pattern.words[0], "bc");
  EXPECT_STREQ(pattern.vars[1].value, "a");
}
