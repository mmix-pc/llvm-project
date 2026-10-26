//===-- Command substitution syntax tests ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/freelist_heap.h"
#include "src/wordexp/shell_syntax.h"
#include "src/wordexp/source.h"
#include "test/UnitTest/Test.h"
#include "test/src/__support/wchar/ISO2022CNFixture.h"

using namespace LIBC_NAMESPACE::internal;
namespace {
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
int fail_after = -1;
size_t live = 0;
struct Input {
  uint32_t data[4096];
  size_t size = 0;
  explicit Input(const char *text) {
    while (*text && size < 4096)
      data[size++] = static_cast<unsigned char>(*text++);
  }
  operator LIBC_NAMESPACE::cpp::span<const uint32_t>() const {
    return {data, size};
  }
};
} // namespace

extern "C" void *__wrap_malloc(size_t size) {
  if (fail_after >= 0 && fail_after-- == 0)
    return nullptr;
  void *p = heap.allocate(size);
  live += p != nullptr;
  return p;
}
extern "C" void __wrap_free(void *p) {
  live -= p != nullptr;
  heap.free(p);
}
extern "C" void *__wrap_realloc(void *old, size_t size) {
  if (fail_after >= 0 && fail_after-- == 0)
    return nullptr;
  void *p = heap.realloc(old, size);
  live += p && !old;
  return p;
}

TEST(LlvmLibcShellSyntaxTest, CompoundLists) {
  const char *valid[] = {
      ")",
      "\n)",
      "echo x)",
      "x=1 >out; cat <in | !wrong)",
      "! cat 2>&1 | sort && echo yes || echo no & wait)",
      "(echo x; (echo y)))",
      "{ echo x; })",
      "if true; then echo yes; elif false; then :; else :; fi)",
      "while false; do :; done; until true; do :; done)",
      "for x in a b; do echo $x; done)",
      "for x\ndo :; done)",
      "for x do :; done)",
      "for x in; do :; done)",
      "case x in (a|b) :;; x) echo ')';& *) :;; esac)",
      "case x in esac)",
      "case x in x) :; esac)",
      "f() { :; }; f)",
      "f()\n(:) >out; f)",
      "echo 'a)b' \"$(case x in x) echo y;; esac)\")",
      "echo ${x:-$(echo nested)} $((1+(2))) $'\\x29')",
      "echo `echo \\`echo nested\\``)",
      "echo a\\\nb # not the closing )\n)",
      "i\\\nf true; th\\\nen :; fi)",
      "cat <<EOF\nnot a delimiter ) \"\nEOF\n)",
      "cat <<'END' <<-\"LAST\"\nfirst )\nEND\n\tsecond\n\tLAST\n)",
      "cat <<EO\\\nF\nEO\\\nF\n)",
      "cat <<'EOF'\n$(unparsed body\nEOF\n)",
      "cat <<EOF\n\"$'literal \\$(literal\n$(case x in x) :;; esac)\nEOF\n)",
  };
  for (auto text : valid) {
    Input input(text);
    size_t before = live;
    auto parsed = scan_word_command(input, 0, false);
    ASSERT_EQ(int(parsed.error), int(WordSyntaxError::NONE));
    EXPECT_EQ(parsed.position, input.size);
    EXPECT_EQ(live, before);
  }
}

TEST(LlvmLibcShellSyntaxTest, InvalidGrammar) {
  const char *invalid[] = {
      "",
      "echo",
      "echo 'x)",
      "echo \"x)",
      "echo \\",
      "echo |)",
      "echo &&)",
      "|| echo)",
      "echo >)",
      "if true; then :;)",
      "if true; fi)",
      "while :; done)",
      "for 1 in a; do :; done)",
      "for x in a do :; done)",
      "case x in a :;; esac)",
      "case x in a) :;;)",
      "f() echo x)",
      "{ :)",
      "echo $(if :; then :))",
      "cat <<END\nmissing\n)",
      "cat <<END)",
      "cat <<EOF\n$(unparsed body\nEOF\n)",
      "cat <<EOF\n$(echo yes |)\nEOF\n)",
      "() )",
      "echo 2>&)",
      "echo ; ;)",
      "! ! echo)",
  };
  for (auto text : invalid) {
    Input input(text);
    size_t before = live;
    auto parsed = scan_word_command(input, 0, false);
    EXPECT_FALSE(bool(parsed));
    EXPECT_LE(parsed.position, input.size);
    EXPECT_EQ(live, before);
  }
}

TEST(LlvmLibcShellSyntaxTest, BackquotesAndOriginalPositions) {
  Input valid("echo \\`echo nested\\``suffix");
  auto parsed = scan_word_command(valid, 0, true);
  ASSERT_TRUE(bool(parsed));
  EXPECT_EQ(parsed.position, valid.size - 6);
  Input invalid("echo |`");
  parsed = scan_word_command(invalid, 0, true);
  EXPECT_FALSE(bool(parsed));
  EXPECT_EQ(parsed.position, invalid.size - 1);
  Input missing("echo \\`");
  EXPECT_FALSE(bool(scan_word_command(missing, 0, true)));
}

TEST(LlvmLibcShellSyntaxTest, OuterQuotesAffectLegacyEscapes) {
  Input body(R"cmd(echo \")\"`)cmd");
  EXPECT_FALSE(bool(scan_word_command(body, 0, true)));
  EXPECT_TRUE(bool(scan_word_command(body, 0, true, true)));
  Input quoted(R"cmd("`echo \")\"`")cmd");
  EXPECT_TRUE(
      bool(parse_word_syntax(quoted, false, {}, word_command_syntax())));
  Input unquoted(R"cmd(`echo \")\"`)cmd");
  EXPECT_FALSE(
      bool(parse_word_syntax(unquoted, false, {}, word_command_syntax())));
}

TEST(LlvmLibcShellSyntaxTest, ArithmeticPrecedenceAndSubshellFallback) {
  Input nested("echo $((echo x); echo y))");
  EXPECT_TRUE(bool(scan_word_command(nested, 0, false)));
  size_t commands = 0, arithmetic = 0;
  struct Counts {
    size_t &commands, &arithmetic;
  } counts{commands, arithmetic};
  WordSyntaxSink sink{&counts, [](void *p, WordToken token) {
                        auto &counts = *static_cast<Counts *>(p);
                        counts.commands +=
                            token.kind == WordToken::Kind::COMMAND;
                        counts.arithmetic +=
                            token.kind == WordToken::Kind::ARITHMETIC;
                        return 0;
                      }};
  Input input("$((echo x); echo y) $((1+(2)))");
  EXPECT_TRUE(
      bool(parse_word_syntax(input, false, sink, word_command_syntax())));
  EXPECT_EQ(commands, size_t(1));
  EXPECT_EQ(arithmetic, size_t(1));
  EXPECT_EQ(
      int(parse_word_syntax(input, true, {}, word_command_syntax()).error),
      int(WordSyntaxError::COMMAND_FORBIDDEN));
  Input incomplete("$((1 + 2");
  EXPECT_EQ(int(parse_word_syntax(incomplete, false, {}, word_command_syntax())
                    .error),
            int(WordSyntaxError::SYNTAX));
}

TEST(LlvmLibcShellSyntaxTest, UnsupportedDelimiterDoesNotGuessBytes) {
  Input input("cat <<$'\\x45'\nE\n)");
  auto parsed = scan_word_command(input, 0, false);
  EXPECT_EQ(int(parsed.error), int(WordSyntaxError::COMMAND_PARSER_REQUIRED));
}

TEST(LlvmLibcShellSyntaxTest, EncodedDollarDocumentDelimiters) {
  const char *valid[] = {
      "$(read x <<$'\\x45OF'\nliteral )\nEOF\nprintf '%s' \"$x\")",
      "$(cat <<$'\\xc4\\xb1'\n$(unparsed\n\xc4\xb1\n)",
      "$(cat <<E$'\\x4f'F\nx\nEOF\n)",
      "$(cat <<$'END\\0discarded'\nx\nEND\n)",
      "$(cat <<EOF\n$(cat <<$'END'\nx\nEND\n)\nEOF\n)",
  };
  WordCommandParser parser(RestartableConverter(true));
  for (auto text : valid) {
    WordSource source;
    ASSERT_EQ(source.assign(text, RestartableConverter(true)), 0);
    EXPECT_TRUE(
        bool(parse_word_syntax(source.scalars(), false, {}, parser.service())));
  }
  WordSource invalid;
  ASSERT_EQ(
      invalid.assign("$(cat <<$'\\xc4'\nx\nX\n)", RestartableConverter(true)),
      0);
  auto result =
      parse_word_syntax(invalid.scalars(), false, {}, parser.service());
  EXPECT_FALSE(bool(result));
  EXPECT_EQ(result.sink_error, EILSEQ);
}

TEST(LlvmLibcShellSyntaxTest, EncodedDelimiterFailureReleasesAllStorage) {
  Input input("$(cat <<$'\\x45OF' <<$'END'\nx\nEOF\ny\nEND\n)");
  WordCommandParser parser(RestartableConverter(true));
  bool complete = false;
  size_t before = live;
  for (int failure = 0; failure < 128 && !complete; ++failure) {
    fail_after = failure;
    auto parsed = parse_word_syntax(input, false, {}, parser.service());
    fail_after = -1;
    complete = bool(parsed);
    if (!complete) {
      EXPECT_EQ(int(parsed.error), int(WordSyntaxError::RESOURCE));
      EXPECT_EQ(parsed.sink_error, ENOMEM);
    }
    EXPECT_EQ(live, before);
  }
  EXPECT_TRUE(complete);
}

TEST(LlvmLibcShellSyntaxTest, SourceDocumentFailureReleasesAllStorage) {
  LIBC_NAMESPACE::testing::ISO2022CNFixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  WordCommandParser parser(converter);
  auto service = parser.service();
  const char *inputs[] = {
      "$(cat <<$'\\x45OF' "
      "<<'\x1b$)G\x0e\"!\x0f'\nx\nEOF\ny\n\x1b$)G\x0e\"!\x0f\n)",
      "`cat <<$'\\x45OF' "
      "<<'\x1b$)G\x0e\"!\x0f'\nx\nEOF\ny\n\x1b$)G\x0e\"!\x0f\n`",
  };
  size_t initial = live;
  for (const char *input : inputs) {
    WordSource source;
    ASSERT_EQ(source.assign(input, converter), 0);
    service.bind_source(service.context, &source);
    size_t before = live;
    bool complete = false;
    for (int failure = 0; failure < 256 && !complete; ++failure) {
      fail_after = failure;
      auto parsed = parse_word_syntax(source.scalars(), false, {}, service);
      fail_after = -1;
      complete = bool(parsed);
      if (!complete) {
        EXPECT_EQ(int(parsed.error), int(WordSyntaxError::RESOURCE));
        EXPECT_EQ(parsed.sink_error, ENOMEM);
      }
      EXPECT_EQ(live, before);
    }
    EXPECT_TRUE(complete);
    service.bind_source(service.context, nullptr);
  }
  EXPECT_EQ(live, initial);
}

TEST(LlvmLibcShellSyntaxTest, PreflightBeforeAnySink) {
  size_t emitted = 0;
  WordSyntaxSink sink{&emitted, [](void *p, WordToken) {
                        ++*static_cast<size_t *>(p);
                        return 0;
                      }};
  Input invalid("${x:=value} $(if true; then echo x)");
  EXPECT_FALSE(
      bool(parse_word_syntax(invalid, false, sink, word_command_syntax())));
  EXPECT_EQ(emitted, size_t(0));
  Input valid("$(case x in x) echo yes;; esac)");
  EXPECT_TRUE(
      bool(parse_word_syntax(valid, false, sink, word_command_syntax())));
  EXPECT_GT(emitted, size_t(0));
  emitted = 0;
  EXPECT_EQ(
      int(parse_word_syntax(valid, true, sink, word_command_syntax()).error),
      int(WordSyntaxError::COMMAND_FORBIDDEN));
  EXPECT_EQ(emitted, size_t(0));
}

TEST(LlvmLibcShellSyntaxTest, AllocationFailuresReleasePendingDocuments) {
  Input input("cat <<A <<B\nx\nA\ny\nB\n)");
  for (int failure = 0; failure != 5; ++failure) {
    size_t before = live;
    fail_after = failure;
    auto parsed = scan_word_command(input, 0, false);
    fail_after = -1;
    EXPECT_EQ(int(parsed.error), int(failure < 4 ? WordSyntaxError::RESOURCE
                                                 : WordSyntaxError::NONE));
    if (failure < 4)
      EXPECT_EQ(parsed.sink_error, ENOMEM);
    EXPECT_EQ(live, before);
  }
  Input legacy("echo x`");
  for (int failure = 0; failure != 3; ++failure) {
    size_t before = live;
    fail_after = failure;
    auto parsed = scan_word_command(legacy, 0, true);
    fail_after = -1;
    EXPECT_EQ(int(parsed.error), int(failure < 2 ? WordSyntaxError::RESOURCE
                                                 : WordSyntaxError::NONE));
    EXPECT_EQ(live, before);
  }
}

TEST(LlvmLibcShellSyntaxTest, BoundedNestingAndInput) {
  Input input("");
  for (size_t i = 0; i < 160; ++i)
    input.data[input.size++] = '(';
  input.data[input.size++] = ':';
  for (size_t i = 0; i < 161; ++i)
    input.data[input.size++] = ')';
  EXPECT_EQ(int(scan_word_command(input, 0, false).error),
            int(WordSyntaxError::NESTING_LIMIT));
  EXPECT_FALSE(bool(scan_word_command(input, input.size + 1, false)));
  input.data[5] = 0;
  EXPECT_FALSE(bool(scan_word_command(input, 0, false)));
}
