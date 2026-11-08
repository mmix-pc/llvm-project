//===-- ISO-2022-CN state machine tests -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fnmatch_macros.h"
#include "hdr/wchar_macros.h"
#include "src/__support/wchar/iso2022_cn.h"
#include "src/__support/wchar/restartable_converter.h"
#include "src/fnmatch/matcher.h"
#include "src/glob/component.h"
#include "src/regex/decoded_text.h"
#include "src/wordexp/dollar_quote.h"
#include "src/wordexp/encoded_fields.h"
#include "src/wordexp/expand.h"
#include "src/wordexp/ifs.h"
#include "src/wordexp/pattern.h"
#include "src/wordexp/quote.h"
#include "src/wordexp/shell_syntax.h"
#include "src/wordexp/source.h"

#ifdef LIBC_TEST_HAS_FILE
#include "src/__support/File/file.h"
#include "src/locale/locale_storage.h"
#include "src/stdio/snprintf.h"
#include "src/wchar/swprintf.h"
#include "src/wchar/swscanf.h"
#endif

#ifdef LIBC_FULL_BUILD
#include "src/__support/libc_errno.h"
#include "src/locale/locale.h"
#include "src/locale/mb_cur_max.h"
#include "src/stdlib/mblen.h"
#include "src/stdlib/mbstowcs.h"
#include "src/stdlib/mbtowc.h"
#include "src/stdlib/wcstombs.h"
#include "src/stdlib/wctomb.h"
#include "src/uchar/c16rtomb.h"
#include "src/uchar/c32rtomb.h"
#include "src/uchar/mbrtoc16.h"
#include "src/uchar/mbrtoc32.h"
#include "src/wchar/btowc.h"
#include "src/wchar/mbrlen.h"
#include "src/wchar/mbrtowc.h"
#include "src/wchar/mbsinit.h"
#include "src/wchar/mbsnrtowcs.h"
#include "src/wchar/mbsrtowcs.h"
#include "src/wchar/wcrtomb.h"
#include "src/wchar/wcsnrtombs.h"
#include "src/wchar/wcsrtombs.h"
#include "src/wchar/wctob.h"
#endif

#include "test/UnitTest/Test.h"
#include "test/src/__support/wchar/ISO2022CNFixture.h"

using LIBC_NAMESPACE::internal::ChineseEncodingData;
using LIBC_NAMESPACE::internal::ISO2022CNState;
using LIBC_NAMESPACE::internal::LocaleDataView;
using Event = ISO2022CNState::Event;

namespace {
using Fixture = LIBC_NAMESPACE::testing::ISO2022CNFixture;

void encoded(ISO2022CNState &state, const ChineseEncodingData &data,
             uint32_t scalar, const char *expected, size_t count) {
  for (size_t capacity = 0; capacity < count; ++capacity) {
    ISO2022CNState copy = state;
    uint8_t bytes[10];
    for (auto &byte : bytes)
      byte = 0xcc;
    auto short_output = copy.encode(scalar, {bytes + 1, capacity}, data);
    ASSERT_FALSE(short_output.has_value());
    EXPECT_EQ(short_output.error(), E2BIG);
    for (auto byte : bytes)
      EXPECT_EQ(byte, uint8_t(0xcc));
    auto retry = copy.encode(scalar, {bytes + 1, count}, data);
    ASSERT_TRUE(retry.has_value());
    EXPECT_EQ(*retry, count);
    for (size_t i = 0; i != count; ++i)
      EXPECT_EQ(bytes[i + 1], uint8_t(expected[i]));
    EXPECT_EQ(bytes[0], uint8_t(0xcc));
    EXPECT_EQ(bytes[count + 1], uint8_t(0xcc));
  }
  uint8_t output[8];
  auto result = state.encode(scalar, output, data);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, count);
  for (size_t i = 0; i != count; ++i)
    EXPECT_EQ(output[i], uint8_t(expected[i]));
}
} // namespace

TEST(LlvmLibcISO2022CNTest, BoundaryPrefixRecreatesStateWithoutCharacters) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  const char *states[] = {"",
                          "\x1b$)A",
                          "\x1b$)G",
                          "\x1b$*H",
                          "\x1b$)A\x0e",
                          "\x1b$)G\x0e",
                          "\x1b$)A\x1b$*H",
                          "\x1b$)G\x1b$*H",
                          "\x1b$)A\x1b$*H\x0e",
                          "\x1b$)G\x1b$*H\x0e"};
  for (const char *wire : states) {
    size_t size = 0;
    while (wire[size])
      ++size;
    mbstate saved{};
    auto consumed = converter.decode(
        nullptr, {reinterpret_cast<const uint8_t *>(wire), size}, saved);
    ASSERT_TRUE(consumed.has_value());
    EXPECT_EQ(*consumed, size_t(-2));
    uint8_t output[MAX_LOCALE_STATE_PREFIX_BYTES];
    for (size_t capacity = 0; capacity <= size; ++capacity) {
      for (auto &byte : output)
        byte = 0xaa;
      auto length = converter.prefix(saved, {output, capacity});
      if (capacity < size) {
        ASSERT_FALSE(length.has_value());
        EXPECT_EQ(length.error(), E2BIG);
        for (auto byte : output)
          EXPECT_EQ(byte, uint8_t(0xaa));
      } else {
        ASSERT_TRUE(length.has_value());
        EXPECT_EQ(*length, size);
        mbstate restored{};
        uint32_t unchanged = 0x1234;
        auto result = converter.decode(&unchanged, {output, *length}, restored);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, size_t(-2));
        EXPECT_EQ(unchanged, uint32_t(0x1234));
        EXPECT_EQ(uint32_t(restored.partial), uint32_t(saved.partial));
        EXPECT_EQ(restored.bytes_stored, saved.bytes_stored);
        EXPECT_EQ(restored.total_bytes, saved.total_bytes);
      }
    }
  }
  mbstate partial{};
  const uint8_t escape = 0x1b;
  ASSERT_TRUE(converter.decode(nullptr, {&escape, 1}, partial).has_value());
  uint8_t untouched[9]{};
  EXPECT_EQ(converter.prefix(partial, untouched).error(), EINVAL);
  EXPECT_EQ(RestartableConverter(true).prefix(partial, untouched).error(),
            EINVAL);
}

TEST(LlvmLibcISO2022CNTest, CommandSliceRestoresDesignationAndWireChoice) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  WordSource source;
  // U+4E2D has both GB and CNS spellings. The CNS bytes must not be replaced
  // by the encoder's preferred initial-state GB spelling.
  const char wire[] = "\x1b$)G$(echo \x0e\"!\x0f)";
  ASSERT_EQ(source.assign(wire, converter), 0);
  LIBC_NAMESPACE::CharVector body;
  ASSERT_EQ(source.command(2, source.scalars().size() - 1, false, false,
                           converter, body),
            0);
  EXPECT_STREQ(body.c_str(), "\x1b$)Gecho \x0e\"!\x0f");
  ASSERT_EQ(
      source.assign("\x1b$)A`echo \x1b$)G\\\"\x0e\"!\x0f\\\"`", converter), 0);
  LIBC_NAMESPACE::CharVector legacy;
  ASSERT_EQ(source.command(1, source.scalars().size() - 1, true, true,
                           converter, legacy),
            0);
  EXPECT_STREQ(legacy.c_str(), "\x1b$)Aecho \x1b$)G\"\x0e\"!\x0f\"");
  ASSERT_EQ(source.assign("`echo \\\x1b$)G\n\x1b$)A\x0e!!\x0f`", converter), 0);
  LIBC_NAMESPACE::CharVector continued;
  ASSERT_EQ(source.command(1, source.scalars().size() - 1, true, true,
                           converter, continued),
            0);
  EXPECT_STREQ(continued.c_str(), "echo \x1b$)G\x1b$)A\x0e!!\x0f");
}

TEST(LlvmLibcISO2022CNTest, StatefulDocumentComparisonRequiresByteAdapter) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  WordCommandParser parser(converter);
  WordSource source;
  ASSERT_EQ(source.assign("$(printf x)", converter), 0);
  EXPECT_TRUE(
      bool(parse_word_syntax(source.scalars(), false, {}, parser.service())));
  const char *documents[] = {"$(cat <<EOF\nx\nEOF\n)",
                             "$(cat <<$'\\x45OF'\nx\nEOF\n)"};
  for (auto text : documents) {
    ASSERT_EQ(source.assign(text, converter), 0);
    auto result =
        parse_word_syntax(source.scalars(), false, {}, parser.service());
    EXPECT_EQ(int(result.error), int(WordSyntaxError::COMMAND_PARSER_REQUIRED));
  }
}

TEST(LlvmLibcISO2022CNTest, StatefulDocumentsCompareOriginalBytes) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  WordCommandParser parser(converter);
  auto service = parser.service();
  struct Calls {
    size_t commands = 0, words = 0;
  };
  const char *valid[] = {
      "$(cat <<EOF\nx\nEOF\n)",
      "$(cat <<$'\\x45OF'\nx\nEOF\n)",
      "$(cat <<'\x1b$)G\x0e\"!\x0f'\n$(unparsed\n\x1b$)G\x0e\"!\x0f\n)",
      "\x1b$)G$(cat <<'END'\nx\nEND\n)",
      "$(cat <<'\x1b$)A\x0e!!\x0f'\n\x1b$)G\x0e\"!\x0f\n) not a "
      "terminator\n\x1b$)A\x0e!!\x0f\n)",
      "$(cat <<-\x1b$)G'\x0e\"!\x0f'\n\t\x1b$)G\x0e\"!\x0f\n)",
      "$(cat <<$'\x1b$)G\x0e\"!\x0f'\n\x1b$)G\x0e\"!\x0f\n)",
      "$(cat "
      "<<$'\\x1b\\x24\\x29\\x47\\x0e\\x22\\x21\\x0f'\n\x1b$)G\x0e\"!\x0f\n)",
      "$(cat <<EOF\n$(cat <<'END'\nx\nEND\n)\nEOF\n)",
      "${absent:-$(cat <<'END'\nx\nEND\n)}",
      "$(cat <<EOF\nE\\\nOF\n)",
      "`cat <<'END'\nx\nEND\n`",
      "`cat <<'\x1b$)G\x0e\"!\x0f'\nx\n\x1b$)G\x0e\"!\x0f\n`",
  };
  for (auto text : valid) {
    WordSource source;
    ASSERT_EQ(source.assign(text, converter), 0);
    EXPECT_EQ(service.bind_source(service.context, &source), nullptr);
    auto result = parse_word_syntax(source.scalars(), false, {}, service);
    EXPECT_TRUE(bool(result));
    EXPECT_TRUE(service.bind_source(service.context, nullptr) == &source);
    Calls calls;
    WordExpansionServices services;
    services.context = &calls;
    services.commands = service;
    services.lookup = [](void *, LIBC_NAMESPACE::cpp::string_view)
        -> LIBC_NAMESPACE::ErrorOr<WordVariable> {
      return WordVariable{false, {}};
    };
    services.source_command = [](void *p, const WordSource &, WordToken,
                                 RestartableConverter,
                                 LIBC_NAMESPACE::CharVector &output) {
      ++static_cast<Calls *>(p)->commands;
      return output.append('x') ? 0 : ENOMEM;
    };
    WordExpansionSink sink{
        &calls,
        [](void *p, LIBC_NAMESPACE::cpp::span<const ExpansionPart> parts) {
          ++static_cast<Calls *>(p)->words;
          return parts.size() == 1 && parts[0].bytes == "x" ? 0 : EINVAL;
        }};
    EXPECT_TRUE(bool(expand_word_fragments(source, false, false, converter,
                                           nullptr, nullptr, services, sink)));
    EXPECT_EQ(calls.commands, size_t(1));
    EXPECT_EQ(calls.words, size_t(1));
    EXPECT_EQ(service.bind_source(service.context, nullptr), nullptr);
    calls = {};
    EXPECT_FALSE(bool(expand_word_fragments(source, true, false, converter,
                                            nullptr, nullptr, services, sink)));
    EXPECT_EQ(calls.commands, size_t(0));
    EXPECT_EQ(calls.words, size_t(0));
    EXPECT_EQ(service.bind_source(service.context, nullptr), nullptr);
  }
  // These spell the same U+4E2D, but not the same document delimiter bytes.
  WordSource mismatch;
  ASSERT_EQ(
      mismatch.assign("$(cat <<'\x1b$)A\x0e!!\x0f'\n\x1b$)G\x0e\"!\x0f\n)",
                      converter),
      0);
  service.bind_source(service.context, &mismatch);
  EXPECT_FALSE(bool(parse_word_syntax(mismatch.scalars(), false, {}, service)));
  service.bind_source(service.context, nullptr);

  WordSource independent;
  ASSERT_EQ(independent.assign("$(cat <<'END'\nx\nEND\n)", converter), 0);
  service.bind_source(service.context, &mismatch);
  auto result = parse_word_syntax(independent.scalars(), false, {}, service);
  EXPECT_EQ(int(result.error), int(WordSyntaxError::COMMAND_PARSER_REQUIRED));
  service.bind_source(service.context, nullptr);
}

TEST(LlvmLibcISO2022CNTest, BoundaryTransitionsAreAtomicAndIdempotent) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  const char *states[] = {"",
                          "\x1b$)A",
                          "\x1b$)G",
                          "\x1b$*H",
                          "\x1b$)A\x0e",
                          "\x1b$)G\x0e",
                          "\x1b$)A\x1b$*H",
                          "\x1b$)G\x1b$*H",
                          "\x1b$)A\x1b$*H\x0e",
                          "\x1b$)G\x1b$*H\x0e"};
  auto same = [](const mbstate &a, const mbstate &b) {
    return a.partial == b.partial && a.bytes_stored == b.bytes_stored &&
           a.total_bytes == b.total_bytes;
  };
  for (const char *from : states) {
    for (const char *to : states) {
      mbstate initial{}, required{};
      size_t a = 0, b = 0;
      while (from[a])
        ++a;
      while (to[b])
        ++b;
      ASSERT_TRUE(converter
                      .decode(nullptr,
                              {reinterpret_cast<const uint8_t *>(from), a},
                              initial)
                      .has_value());
      ASSERT_TRUE(converter
                      .decode(nullptr,
                              {reinterpret_cast<const uint8_t *>(to), b},
                              required)
                      .has_value());
      uint8_t output[MAX_LOCALE_STATE_PREFIX_BYTES];
      auto updated = initial;
      auto length = converter.transition(required, updated, output);
      ASSERT_TRUE(length.has_value());
      auto observed = initial;
      uint32_t no_character = 0x1234;
      ASSERT_TRUE(converter.decode(&no_character, {output, *length}, observed)
                      .has_value());
      EXPECT_EQ(no_character, uint32_t(0x1234));
      EXPECT_TRUE(same(observed, updated));
      EXPECT_TRUE(converter.boundary(updated));
      // Applying the same requirement again must not add filename bytes.
      EXPECT_EQ(*converter.transition(required, updated, output), size_t(0));
      for (size_t capacity = 0; capacity < *length; ++capacity) {
        for (auto &byte : output)
          byte = 0xaa;
        auto unchanged = initial;
        EXPECT_EQ(converter.transition(required, unchanged, {output, capacity})
                      .error(),
                  E2BIG);
        EXPECT_TRUE(same(unchanged, initial));
        for (auto byte : output)
          EXPECT_EQ(byte, uint8_t(0xaa));
      }
      // Unused designations may remain, but the next character's meaning must
      // agree with the required state, including its active G1 selection.
      const uint8_t *next;
      size_t count;
      if (required.total_bytes & 8) {
        next = reinterpret_cast<const uint8_t *>(
            (required.total_bytes & 3) == 1 ? "!!" : "\"!");
        count = 2;
      } else {
        next = reinterpret_cast<const uint8_t *>("X");
        count = 1;
      }
      uint32_t expected = 0, actual = 0;
      ASSERT_TRUE(
          converter.decode(&expected, {next, count}, required).has_value());
      ASSERT_TRUE(
          converter.decode(&actual, {next, count}, updated).has_value());
      EXPECT_EQ(expected, actual);
    }
  }
}

TEST(LlvmLibcISO2022CNTest, LiteralExpansionPreservesWireChoice) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  WordSource source;
  // The designation belongs to a removed quote. Reconstruct that state for
  // the selected literal instead of changing its CNS spelling to GB.
  ASSERT_EQ(source.assign("\x1b$)G'\x0e\"!\x0f'X", converter), 0);
  LIBC_NAMESPACE::CharVector result;
  auto word = [](void *p,
                 LIBC_NAMESPACE::cpp::span<const ExpansionPart> parts) {
    auto &out = *static_cast<LIBC_NAMESPACE::CharVector *>(p);
    for (const auto &part : parts)
      for (char byte : part.bytes)
        if (!out.append(byte))
          return ENOMEM;
    return 0;
  };
  ASSERT_TRUE(bool(expand_word_fragments(
      source, true, false, converter, nullptr, nullptr, {}, {&result, word})));
  EXPECT_STREQ(result.c_str(), "\x1b$)G\x0e\"!\x0fX");
  WordSource decoded;
  ASSERT_EQ(decoded.assign({result.c_str(), result.length()}, converter), 0);
  ASSERT_EQ(decoded.scalars().size(), size_t(2));
  EXPECT_EQ(decoded.scalars()[0], uint32_t(0x4e2d));
  EXPECT_EQ(decoded.scalars()[1], uint32_t('X'));
}

TEST(LlvmLibcISO2022CNTest, SourceSuffixBelongsOnlyToAdjacentWord) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  struct Output {
    LIBC_NAMESPACE::CharVector bytes;
    size_t words = 0;
    static int word(void *p,
                    LIBC_NAMESPACE::cpp::span<const ExpansionPart> parts) {
      auto &self = *static_cast<Output *>(p);
      ++self.words;
      for (const auto &part : parts)
        for (char c : part.bytes)
          if (!self.bytes.append(c))
            return ENOMEM;
      return 0;
    }
  };
  const struct {
    const char *source, *expected;
    size_t words;
    int error;
  } cases[] = {
      {"\x1b$)G\x0e\"!\x0f\x1b$)A", "\x1b$)G\x0e\"!\x0f\x1b$)A", 1, 0},
      {"X\x1b$)A\x0e", "X\x1b$)A\x0e", 1, 0},
      {"''\x1b$)G", "\x1b$)G", 1, 0},
      {"\x1b$)G", "", 0, 0},
      {" \x1b$)G", "", 0, 0},
      {"X \x1b$)G", "X", 1, 0},
      {"$'\\377'\x1b$)G", "", 0, EILSEQ},
      {"$'\\x1b'\x1b$)G", "", 0, EILSEQ},
      {"$'\\x1b\\x24\\x29\\x47\\x0e\\x22\\x21'\x0f", "\x1b$)G\x0e\"!\x0f", 1,
       0},
  };
  for (auto row : cases) {
    WordSource source;
    ASSERT_EQ(source.assign(row.source, converter), 0);
    Output output;
    auto result = expand_word_fragments(source, true, false, converter, nullptr,
                                        nullptr, {}, {&output, Output::word});
    EXPECT_EQ(bool(result), row.error == 0);
    EXPECT_EQ(result.system_error, row.error);
    EXPECT_EQ(output.words, row.words);
    EXPECT_STREQ(output.bytes.c_str(), row.expected);
  }
}

TEST(LlvmLibcISO2022CNTest, DollarQuoteTracksNumericWireState) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  const struct {
    const char *source, *expected;
    int error;
  } cases[] = {
      {"\x1b$)G\x0e\"!\x0f", "\x1b$)G\x0e\"!\x0f", 0},
      {"\\x1b\\x24\\x29\\x47\\x0e\\x22\\x21\\t", "\x1b$)G\x0e\"!\x0f\t", 0},
      {"\\x1b\\x24\\x29\\x47\\x0e\\x22\\x21", "\x1b$)G\x0e\"!", 0},
      {"\\x1b\\x24\\x29\\x47\\x0e", "\x1b$)G\x0e", 0},
      {"\\377", "\xff", 0},
      {"\\x1b", "\x1b", 0},
      {"\\x1bZ", "\x1b", EILSEQ},
      {"\\e", "", EILSEQ},
      {"\\0\\e", "", 0},
  };
  for (auto row : cases) {
    WordSource source;
    ASSERT_EQ(source.assign(row.source, converter), 0);
    LIBC_NAMESPACE::CharVector bytes;
    auto result =
        render_word_quote(source, 0, source.scalars().size(), converter, bytes);
    EXPECT_EQ(bool(result), row.error == 0);
    if (row.error) {
      EXPECT_TRUE(result.error == WordSyntaxError::SINK_ERROR);
      EXPECT_EQ(result.sink_error, row.error);
    }
    EXPECT_STREQ(bytes.c_str(), row.expected);
  }
}

TEST(LlvmLibcISO2022CNTest,
     DollarQuotesComposeWithSourceAndNestedAlternatives) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  auto word = [](void *p,
                 LIBC_NAMESPACE::cpp::span<const ExpansionPart> parts) {
    auto &out = *static_cast<LIBC_NAMESPACE::CharVector *>(p);
    for (const auto &part : parts)
      for (char c : part.bytes)
        if (!out.append(c))
          return ENOMEM;
    return 0;
  };
  WordExpansionServices services;
  services.lookup = [](void *, LIBC_NAMESPACE::cpp::string_view)
      -> LIBC_NAMESPACE::ErrorOr<WordVariable> {
    return WordVariable{false, {}};
  };
  const struct {
    const char *source, *expected;
  } cases[] = {
      {"$'\\x1b\\x24\\x29\\x47\\x0e\\x22\\x21'", "\x1b$)G\x0e\"!"},
      {"$'\\x1b'", "\x1b"},
      {"$'\\x1b'$'\\x24\\x29\\x47\\x0e\\x22\\x21'", "\x1b$)G\x0e\"!"},
      {"$'\\x1b'$'\\x24\\x29\\x47\\x0e\\x22\\x21'$'\\t'",
       "\x1b$)G\x0e\"!\x0f\t"},
      {"$'\\377'", "\xff"},
      {"\x1b$)G$'\x0e\"!\x0f'Z", "\x1b$)G\x0e\"!\x0fZ"},
      {"${absent:-\x1b$)G$'\x0e\"!\x0f'}", "\x1b$)G\x0e\"!\x0f"},
      {"$'\\0\\e'Z", "Z"},
  };
  for (auto row : cases) {
    WordSource source;
    ASSERT_EQ(source.assign(row.source, converter), 0);
    LIBC_NAMESPACE::CharVector output;
    ASSERT_TRUE(
        bool(expand_word_fragments(source, true, false, converter, nullptr,
                                   nullptr, services, {&output, word})));
    EXPECT_STREQ(output.c_str(), row.expected);
  }
}

TEST(LlvmLibcISO2022CNTest, WordSourceRetainsWireStates) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  WordSource source;
  const char wire[] = "'\x1b$)A\x0e!!\x0f'\x1b$)G";
  ASSERT_EQ(source.assign(wire, converter), 0);
  ASSERT_EQ(source.scalars().size(), size_t(3));
  EXPECT_EQ(source.scalars()[1], uint32_t(0x4e2d));
  EXPECT_TRUE(*source.slice(1, 2) == "\x1b$)A\x0e!!");
  EXPECT_TRUE(*source.slice(2, 3) == "\x0f'");
  EXPECT_TRUE(source.suffix() == "\x1b$)G");
  EXPECT_TRUE(converter.initial(source.units()[1].before));
  EXPECT_FALSE(converter.initial(source.units()[1].after));
  // Resume from the saved boundary without injecting a designation or
  // re-encoding the spelling chosen by the caller.
  auto state = source.units()[2].before;
  auto bytes = *source.slice(2, 3);
  uint32_t scalar;
  auto decoded = converter.decode(
      &scalar, {reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()},
      state);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, bytes.size());
  EXPECT_EQ(scalar, uint32_t('\''));
  EXPECT_TRUE(converter.boundary(source.end_state()));
  EXPECT_FALSE(converter.initial(source.end_state()));
  ASSERT_EQ(source.assign("\x1b$)A", converter), 0);
  EXPECT_TRUE(source.scalars().empty());
  EXPECT_TRUE(source.suffix() == "\x1b$)A");
  EXPECT_EQ(source.assign("\x1b$)A\x0e!", converter), EILSEQ);
  EXPECT_TRUE(source.suffix() == "\x1b$)A");
}

TEST(LlvmLibcISO2022CNTest, FieldDelimitersPreserveSourceBytes) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  WordDelimiters ifs;
  ASSERT_EQ(ifs.assign(true, "\x1b$)A\x0e!!!\"\x0f ", converter), 0);
  ASSERT_EQ(ifs.delimiters().size(), size_t(3));
  EXPECT_TRUE(ifs.delimiters()[0].bytes == "\x1b$)A\x0e!!");
  // The second character does not invent a new designation or shift prefix.
  EXPECT_TRUE(ifs.delimiters()[1].bytes == "!\"");
  EXPECT_TRUE(ifs.delimiters()[2].bytes == " ");
  EXPECT_TRUE(ifs.delimiters()[2].whitespace);
  ASSERT_EQ(ifs.assign(true, "\x1b$)A\x0e!!\x0f", converter), 0);
  EXPECT_EQ(ifs.delimiters().size(), size_t(1));
  EXPECT_EQ(ifs.assign(true, "\x1b$", converter), EILSEQ);
  EXPECT_TRUE(ifs.delimiters()[0].bytes == "\x1b$)A\x0e!!");
  ASSERT_EQ(ifs.assign(true, "\x1b$)A", converter), 0);
  EXPECT_TRUE(ifs.delimiters().empty());
}

namespace {
struct FieldOutput {
  char bytes[8][128]{};
  size_t count = 0, size = 0, calls = 0, fail_at = size_t(-1);
  LIBC_NAMESPACE::internal::FieldSink sink() { return {this, append, finish}; }
  static int append(void *p, LIBC_NAMESPACE::cpp::string_view input, bool) {
    auto &self = *static_cast<FieldOutput *>(p);
    if (self.calls++ == self.fail_at)
      return ENOMEM;
    if (self.count == 8 || input.size() >= 128 - self.size)
      return EOVERFLOW;
    for (char c : input)
      self.bytes[self.count][self.size++] = c;
    self.bytes[self.count][self.size] = 0;
    return 0;
  }
  static int finish(void *p) {
    auto &self = *static_cast<FieldOutput *>(p);
    if (self.calls++ == self.fail_at)
      return ENOMEM;
    ++self.count;
    self.size = 0;
    return 0;
  }
};
} // namespace

TEST(LlvmLibcISO2022CNTest, FieldSplittingMatchesCharactersNotGraphicBytes) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  WordDelimiters ifs;
  ASSERT_EQ(ifs.assign(true, "!", converter), 0);
  ExpansionPart parts[] = {{"\x1b$)A\x0e!!\x0f!suffix", true, false}};
  FieldOutput output;
  auto split = split_encoded_word_fields(parts, ifs.delimiters(), converter,
                                         output.sink());
  ASSERT_TRUE(split.has_value());
  ASSERT_EQ(*split, size_t(2));
  EXPECT_STREQ(output.bytes[0], "\x1b$)A\x0e!!\x0f");
  EXPECT_STREQ(output.bytes[1], "suffix");

  // The same scalar has GB and CNS spellings. Match the character, but retain
  // the CNS spelling and state of the following non-delimiter character.
  ASSERT_EQ(ifs.assign(true, "\x1b$)A\x0e!!\x0f", converter), 0);
  parts[0].bytes = "a\x1b$)G\x0e\"!\"\"\x0f";
  FieldOutput alternate;
  split = split_encoded_word_fields(parts, ifs.delimiters(), converter,
                                    alternate.sink());
  ASSERT_TRUE(split.has_value());
  ASSERT_EQ(*split, size_t(2));
  EXPECT_STREQ(alternate.bytes[0], "a");
  EXPECT_STREQ(alternate.bytes[1], "\x1b$)G\x0e\"\"\x0f");
  ASSERT_EQ(ifs.assign(true, ":", converter), 0);
  parts[0].bytes = "\x1b$)A\x0e!!\x0f:d\x1b$)A\x0e!\"\x0f/leaf";
  FieldOutput explicit_prefix;
  split = split_encoded_word_fields(parts, ifs.delimiters(), converter,
                                    explicit_prefix.sink());
  ASSERT_TRUE(split.has_value());
  ASSERT_EQ(*split, size_t(2));
  EXPECT_STREQ(explicit_prefix.bytes[0], "\x1b$)A\x0e!!\x0f");
  EXPECT_STREQ(explicit_prefix.bytes[1], "d\x1b$)A\x0e!\"\x0f/leaf");
}

TEST(LlvmLibcISO2022CNTest, FieldSplittingRetainsOriginsAndEmptyQuotes) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  WordDelimiters ifs;
  ASSERT_EQ(ifs.assign(true, "\x1b$)A\x0e!!\x0f", converter), 0);
  ExpansionPart parts[] = {{"\x1b$)A\x0e!", true, false},
                           {"!\x0f", true, false}};
  FieldOutput output;
  auto split = split_encoded_word_fields(parts, ifs.delimiters(), converter,
                                         output.sink());
  ASSERT_TRUE(split.has_value());
  ASSERT_EQ(*split, size_t(1));
  EXPECT_STREQ(output.bytes[0], "\x1b$)A\x0e!!\x0f");
  ExpansionPart empty[] = {{"\x1b$)A\x0e!!\x0f", true, false},
                           {"", false, true}};
  FieldOutput quoted;
  split = split_encoded_word_fields(empty, ifs.delimiters(), converter,
                                    quoted.sink());
  ASSERT_TRUE(split.has_value());
  ASSERT_EQ(*split, size_t(2));
  EXPECT_STREQ(quoted.bytes[0], "");
  EXPECT_STREQ(quoted.bytes[1], "");
  ExpansionPart suffix[] = {{"a \x1b$)A", true, false}};
  FieldOutput trailing;
  split = split_encoded_word_fields(suffix, DEFAULT_FIELD_DELIMITERS, converter,
                                    trailing.sink());
  ASSERT_TRUE(split.has_value());
  ASSERT_EQ(*split, size_t(1));
  EXPECT_STREQ(trailing.bytes[0], "a");
}

TEST(LlvmLibcISO2022CNTest, FieldSplittingStopsAtEverySinkError) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  ExpansionPart parts[] = {{"a \x1b$)A\x0e!!\x0f b", true, false}};
  FieldOutput baseline;
  ASSERT_TRUE(split_encoded_word_fields(parts, DEFAULT_FIELD_DELIMITERS,
                                        converter, baseline.sink())
                  .has_value());
  for (size_t failure = 0; failure != baseline.calls; ++failure) {
    FieldOutput output;
    output.fail_at = failure;
    auto result = split_encoded_word_fields(parts, DEFAULT_FIELD_DELIMITERS,
                                            converter, output.sink());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ENOMEM);
    EXPECT_EQ(output.calls, failure + 1);
  }
  ExpansionPart raw[] = {{"\xff", false, true}};
  FieldOutput quoted;
  ASSERT_TRUE(split_encoded_word_fields(raw, DEFAULT_FIELD_DELIMITERS,
                                        converter, quoted.sink())
                  .has_value());
  EXPECT_STREQ(quoted.bytes[0], "\xff");
  raw[0].split = true;
  raw[0].quoted = false;
  FieldOutput invalid;
  EXPECT_EQ(split_encoded_word_fields(raw, DEFAULT_FIELD_DELIMITERS, converter,
                                      invalid.sink())
                .error(),
            EILSEQ);
}

TEST(LlvmLibcISO2022CNTest, ParameterRemovalReencodesRemainingCharacters) {
  using namespace LIBC_NAMESPACE::internal;
  using Op = ParameterExpansion::Operation;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  const char value[] = "\x1b$)A\x0e!!!\"\x0f";
  auto length = word_parameter_length(value, converter);
  ASSERT_TRUE(length.has_value());
  EXPECT_EQ(*length, size_t(2));
  LIBC_NAMESPACE::CharVector suffix;
  auto removed = remove_word_pattern("?", value, Op::PREFIX, converter, nullptr,
                                     nullptr, suffix);
  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(*removed, size_t(1));
  EXPECT_STREQ(suffix.c_str(), "\x1b$)A\x0e!\"\x0f");
  LIBC_NAMESPACE::CharVector prefix;
  removed = remove_word_pattern("?", value, Op::SUFFIX, converter, nullptr,
                                nullptr, prefix);
  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(*removed, size_t(1));
  EXPECT_STREQ(prefix.c_str(), "\x1b$)A\x0e!!\x0f");
  LIBC_NAMESPACE::CharVector no_change;
  removed = remove_word_pattern("*", value, Op::PREFIX, converter, nullptr,
                                nullptr, no_change);
  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(*removed, size_t(0));
  EXPECT_STREQ(no_change.c_str(), value);
}

TEST(LlvmLibcISO2022CNTest, DollarQuoteControlsAreNotRawBytes) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  struct Sink {
    RestartableConverter converter;
    mbstate state{};
    uint8_t bytes[32]{};
    size_t size = 0;
    static int emit(void *opaque, QuotedUnit unit) {
      auto &self = *static_cast<Sink *>(opaque);
      if (unit.kind == QuotedUnit::Kind::BYTE) {
        self.bytes[self.size++] = uint8_t(unit.value);
        return 0;
      }
      auto result = self.converter.encode(
          unit.value, {self.bytes + self.size, sizeof(self.bytes) - self.size},
          self.state);
      if (!result)
        return result.error();
      self.size += *result;
      return 0;
    }
  };
  Sink sink{RestartableConverter(*mapping)};
  const uint32_t esc[] = {'\\', 'e'};
  auto error = decode_dollar_quote(esc, {&sink, Sink::emit});
  EXPECT_TRUE(error.error == WordSyntaxError::SINK_ERROR);
  EXPECT_EQ(error.sink_error, EILSEQ);
  EXPECT_EQ(sink.size, size_t(0));
  EXPECT_TRUE(sink.converter.initial(sink.state));
  const uint32_t shift[] = {'\\', 'c', 'n'};
  error = decode_dollar_quote(shift, {&sink, Sink::emit});
  EXPECT_EQ(error.sink_error, EILSEQ);
  EXPECT_EQ(sink.size, size_t(0));
  const uint32_t bytes[] = {'\\', 'x', '1', 'b', '\\', '3', '3'};
  ASSERT_TRUE(bool(decode_dollar_quote(bytes, {&sink, Sink::emit})));
  EXPECT_EQ(sink.size, size_t(2));
  EXPECT_EQ(sink.bytes[0], uint8_t(27));
  EXPECT_EQ(sink.bytes[1], uint8_t(27));
  // Raw numeric bytes are emitted as requested, not submitted as characters.
  EXPECT_TRUE(sink.converter.initial(sink.state));
  const uint32_t discarded[] = {'\\', '0', '\\', 'e'};
  ASSERT_TRUE(bool(decode_dollar_quote(discarded, {&sink, Sink::emit})));
  EXPECT_EQ(sink.size, size_t(2));
  const uint32_t scalar[] = {0x4e2d};
  ASSERT_TRUE(bool(decode_dollar_quote(scalar, {&sink, Sink::emit})));
  EXPECT_EQ(sink.size, size_t(9));
  EXPECT_EQ(sink.bytes[7], uint8_t('!'));
  EXPECT_EQ(sink.bytes[8], uint8_t('!'));
}

TEST(LlvmLibcISO2022CNTest, RegexOffsetsKeepShiftPrefixesAndSuffixes) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto mapping = fixture.read();
  ASSERT_TRUE(mapping.has_value());
  RestartableConverter converter(*mapping);
  DecodedRegexText text;
  const char input[] = "\x1b$)A\x0e!!\x0f";
  ASSERT_EQ(text.assign({input, sizeof(input) - 1}, converter), 0);
  ASSERT_EQ(text.units().size(), size_t(1));
  EXPECT_EQ(text.units()[0].scalar, uint32_t(0x4e2d));
  EXPECT_EQ(text.units()[0].begin, regoff_t(0));
  EXPECT_EQ(text.units()[0].end, regoff_t(7));
  EXPECT_EQ(text.characters_end(), regoff_t(7));
  EXPECT_EQ(text.bytes(), regoff_t(8));
  ASSERT_EQ(text.assign("\x1b$)A\x0e!!\x0f!", converter), 0);
  ASSERT_EQ(text.units().size(), size_t(2));
  EXPECT_EQ(text.units()[1].scalar, uint32_t('!'));
  EXPECT_EQ(text.units()[1].begin, regoff_t(7));
  EXPECT_EQ(text.units()[1].end, regoff_t(9));
  ASSERT_EQ(text.assign("\x1b$)A", converter), 0);
  EXPECT_EQ(text.units().size(), size_t(0));
  EXPECT_EQ(text.characters_end(), regoff_t(0));
  EXPECT_EQ(text.bytes(), regoff_t(4));
  EXPECT_EQ(text.assign("\x1b$)A\x0e!", converter), EILSEQ);
  EXPECT_EQ(text.assign("\x1b$", converter), EILSEQ);
  EXPECT_EQ(text.bytes(), regoff_t(4));
}

TEST(LlvmLibcISO2022CNTest, GlobSyntaxUsesCharactersNotEncodedBytes) {
  using namespace LIBC_NAMESPACE::internal;
  const uint32_t codes[] = {0x2a3fU, 0x5b5cU, 0x5c5dU};
  for (uint32_t code : codes) {
    Fixture fixture;
    // Replace one independently constructed GB mapping with syntax-like bytes.
    Fixture::put(fixture.bytes + 234, code);
    Fixture::put(fixture.bytes + 254, code);
    auto data = fixture.read();
    ASSERT_TRUE(data.has_value());
    char wire[] = "\x1b$)A\x0eXX\x0f";
    wire[5] = char(code >> 8);
    wire[6] = char(code);
    auto parsed = prepare_glob_component(wire, sizeof(wire) - 1, false,
                                         false, RestartableConverter(*data));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed->magic);
    EXPECT_EQ(parsed->length, sizeof(wire) - 1);
    EXPECT_EQ(wire[5], char(code >> 8));
    EXPECT_EQ(wire[6], char(code));
    char quoted[] = "\\\x1b$)A\x0eXX\x0f\\*";
    quoted[6] = char(code >> 8);
    quoted[7] = char(code);
    parsed = prepare_glob_component(quoted, sizeof(quoted) - 1, false,
                                    false, RestartableConverter(*data));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed->magic);
    EXPECT_EQ(parsed->length, size_t(9));
    for (unsigned i = 0; i != 8; ++i)
      EXPECT_EQ(quoted[i], wire[i]);
    EXPECT_EQ(quoted[8], '*');
    EXPECT_EQ(quoted[9], '\0');
  }
}

#ifdef LIBC_TEST_HAS_FILE
namespace {
class StateStream : public LIBC_NAMESPACE::File {
  uint8_t buffer[16]{};
  static LIBC_NAMESPACE::FileIOResult input(File *base, void *out,
                                            size_t count) {
    auto &f = *static_cast<StateStream *>(base);
    size_t n = 0;
    while (n < count && f.offset < f.length)
      static_cast<uint8_t *>(out)[n++] = f.bytes[f.offset++];
    return n;
  }
  static LIBC_NAMESPACE::FileIOResult output(File *base, const void *in,
                                             size_t count) {
    auto &f = *static_cast<StateStream *>(base);
    size_t n = 0;
    while (n < count && f.offset < f.stop && f.offset < sizeof(f.bytes))
      f.bytes[f.offset++] = static_cast<const uint8_t *>(in)[n++];
    if (f.offset > f.length)
      f.length = f.offset;
    return n == count ? LIBC_NAMESPACE::FileIOResult(n)
                      : LIBC_NAMESPACE::FileIOResult(n, EINTR);
  }
  static LIBC_NAMESPACE::ErrorOr<off_t> position(File *base, off_t at,
                                                 int how) {
    auto &f = *static_cast<StateStream *>(base);
    off_t value = at + (how == SEEK_CUR   ? f.offset
                        : how == SEEK_END ? f.length
                                          : 0);
    if (value < 0 || size_t(value) > sizeof(f.bytes))
      return LIBC_NAMESPACE::Error(EINVAL);
    f.offset = size_t(value);
    return value;
  }
  static int finish(File *) { return 0; }

public:
  uint8_t bytes[128]{};
  size_t offset = 0, length = 0, stop = sizeof(bytes);
  explicit StateStream(int mode = _IOFBF)
      : File(output, input, position, finish, buffer, sizeof(buffer), mode,
             false, File::mode_flags("w+")) {}
  ~StateStream() { close(); }
  template <size_t N> void fill(const char (&wire)[N]) {
    length = N - 1;
    for (size_t i = 0; i < length; ++i)
      bytes[i] = uint8_t(wire[i]);
  }
};

struct SelectedEncoding {
  __locale_data category;
  __locale_t locale;
  locale_t previous;
  explicit SelectedEncoding(const ChineseEncodingData &data)
      : category("codec-test", false),
        locale(LIBC_NAMESPACE::internal::locale_from_mask(0)),
        previous(LIBC_NAMESPACE::internal::thread_locale) {
    category.chinese_encoding = data;
    locale.__data[LC_CTYPE] = &category;
    LIBC_NAMESPACE::internal::thread_locale = &locale;
  }
  ~SelectedEncoding() { LIBC_NAMESPACE::internal::thread_locale = previous; }
};
} // namespace

TEST(LlvmLibcISO2022CNTest, FormattedStringsRetainShiftsAndPrecision) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  SelectedEncoding locale(*data);
  const char wire[] = "\x1b$)A\x0e!!!\"\x0f";
  char bytes[32];
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(bytes, sizeof(bytes), "%ls", L"\u4e2d\u6587"),
      10);
  EXPECT_STREQ(bytes, wire);
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(bytes, sizeof(bytes), "%.6ls", L"\u4e2d\u6587"),
      0);
  EXPECT_STREQ(bytes, "");
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(bytes, sizeof(bytes), "%.7ls", L"\u4e2d\u6587"),
      7);
  EXPECT_STREQ(bytes, "\x1b$)A\x0e!!");
  EXPECT_EQ(
      LIBC_NAMESPACE::snprintf(bytes, sizeof(bytes), "%.9ls", L"\u4e2d\u6587"),
      9);
  EXPECT_STREQ(bytes, "\x1b$)A\x0e!!!\"");
  wchar_t output[8];
  EXPECT_EQ(LIBC_NAMESPACE::swprintf(output, 8, L"%s", wire), 2);
  EXPECT_EQ(output[0], wchar_t(0x4e2d));
  EXPECT_EQ(output[1], wchar_t(0x6587));
  EXPECT_EQ(output[2], wchar_t(0));
  // Invalid input after the selected prefix must not be examined.
  EXPECT_EQ(LIBC_NAMESPACE::swprintf(output, 8, L"%.1s", "\x1b$)A\x0e!!\xff"),
            1);
  EXPECT_EQ(LIBC_NAMESPACE::swprintf(output, 8, L"%s", "\x0e!!"), -1);
  EXPECT_EQ(LIBC_NAMESPACE::swprintf(output, 8, L"%c", 0x1b), -1);
}

TEST(LlvmLibcISO2022CNTest, WideScanOutputResetsStringsButNotCharacters) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  SelectedEncoding locale(*data);
  char bytes[32];
  EXPECT_EQ(LIBC_NAMESPACE::swscanf(L"\u4e2d\u6587", L"%s", bytes), 1);
  EXPECT_STREQ(bytes, "\x1b$)A\x0e!!!\"\x0f");
  // CNS plane 2 needs eight bytes even for one input wide character.
  EXPECT_EQ(LIBC_NAMESPACE::swscanf(L"\U00020000", L"%s", bytes), 1);
  EXPECT_STREQ(bytes, "\x1b$*H\x1bN#!");
  for (auto &c : bytes)
    c = '!';
  EXPECT_EQ(LIBC_NAMESPACE::swscanf(L"\u4e2d", L"%c", bytes), 1);
  const char expected[] = "\x1b$)A\x0e!!";
  for (size_t i = 0; i < sizeof(expected) - 1; ++i)
    EXPECT_EQ(bytes[i], expected[i]);
  EXPECT_EQ(bytes[7], '!');
}

TEST(LlvmLibcISO2022CNTest, StreamOutputStateAndShortWriteRecovery) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  SelectedEncoding locale(*data);
  const char expected[] = "\x1b$)A\x0e!!!\"\x0f"
                          "A";
  for (size_t stop = 0; stop < sizeof(expected) - 1; ++stop) {
    StateStream stream;
    ASSERT_EQ(stream.write(L"\u4e2d\u6587A", 3).value, size_t(3));
    EXPECT_EQ(stream.tell().value(), off_t(sizeof(expected) - 1));
    auto logical = stream.get_position();
    ASSERT_TRUE(logical.has_value());
    stream.stop = stop;
    EXPECT_EQ(stream.flush(), EINTR);
    EXPECT_EQ(stream.offset, stop);
    EXPECT_EQ(stream.tell().value(), logical->offset);
    stream.stop = sizeof(stream.bytes);
    stream.clearerr();
    ASSERT_EQ(stream.flush(), 0);
    EXPECT_EQ(stream.length, sizeof(expected) - 1);
    for (size_t i = 0; i < sizeof(expected) - 1; ++i)
      EXPECT_EQ(stream.bytes[i], uint8_t(expected[i]));
    ASSERT_EQ(stream.write(L"\u4e2d", 1).value, size_t(1));
    ASSERT_EQ(stream.flush(), 0);
    EXPECT_EQ(stream.length, sizeof(expected) + 2);
    EXPECT_EQ(stream.bytes[sizeof(expected) - 1], uint8_t(0x0e));
  }
}

TEST(LlvmLibcISO2022CNTest, StreamLookaheadAndPositionRetainActualWireState) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  SelectedEncoding locale(*data);
  StateStream stream;
  // Redundant designation makes re-encoding an invalid position observer.
  stream.fill("\x1b$)A\x1b$)A\x0e!!!\"\x0f"
              "A");
  wchar_t value;
  ASSERT_EQ(stream.peek_wide_unlocked(&value).value, size_t(1));
  EXPECT_EQ(value, wchar_t(0x4e2d));
  EXPECT_EQ(stream.tell().value(), off_t(0));
  auto initial = stream.get_position();
  ASSERT_TRUE(initial.has_value());
  ASSERT_EQ(stream.flush(), 0);
  EXPECT_EQ(stream.offset, size_t(0));
  ASSERT_EQ(stream.read(&value, 1).value, size_t(1));
  EXPECT_EQ(value, wchar_t(0x4e2d));
  auto shifted = stream.get_position();
  ASSERT_TRUE(shifted.has_value());
  EXPECT_EQ(shifted->offset, off_t(11));
  EXPECT_FALSE(ISO2022CNState::load(shifted->state)->initial());
  ASSERT_EQ(stream.ungetwc(value).value(), wint_t(value));
  ASSERT_EQ(stream.read(&value, 1).value, size_t(1));
  EXPECT_EQ(stream.tell().value(), shifted->offset);
  ASSERT_EQ(stream.peek_wide_unlocked(&value).value, size_t(1));
  EXPECT_EQ(value, wchar_t(0x6587));
  EXPECT_EQ(stream.tell().value(), shifted->offset);
  ASSERT_EQ(stream.flush(), 0);
  ASSERT_EQ(stream.read(&value, 1).value, size_t(1));
  EXPECT_EQ(value, wchar_t(0x6587));
  ASSERT_EQ(stream.set_position(*shifted).value(), 0);
  ASSERT_EQ(stream.read(&value, 1).value, size_t(1));
  EXPECT_EQ(value, wchar_t(0x6587));
  ASSERT_EQ(stream.set_position(*initial).value(), 0);
  ASSERT_EQ(stream.read(&value, 1).value, size_t(1));
  EXPECT_EQ(value, wchar_t(0x4e2d));
}

TEST(LlvmLibcISO2022CNTest, StreamEofDistinguishesShiftAndIncompleteCharacter) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  SelectedEncoding locale(*data);
  StateStream complete;
  complete.fill("\x1b$)A\x0e!!\x0f");
  wchar_t values[2];
  auto result = complete.read(values, 2);
  EXPECT_EQ(result.value, size_t(1));
  EXPECT_EQ(result.error, 0);
  EXPECT_TRUE(complete.iseof());
  StateStream partial;
  partial.fill("\x1b$)A\x0e!");
  result = partial.read(values, 1);
  EXPECT_EQ(result.value, size_t(0));
  EXPECT_EQ(result.error, EILSEQ);
}

TEST(LlvmLibcISO2022CNTest, StreamRetainsOwnedMappingAfterLocaleRelease) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto file = LocaleDataFile::copy({fixture.bytes, 322});
  ASSERT_TRUE(file.has_value());
  auto category = create_locale_category(**file, false);
  ASSERT_TRUE(category.has_value());
  auto data = ChineseEncodingData::read((*file)->view());
  ASSERT_TRUE(data.has_value());
  (*category)->chinese_encoding = *data;
  (*file)->release();
  auto locale = locale_from_mask(0);
  locale.__data[LC_CTYPE] = *category;
  auto previous = thread_locale;
  thread_locale = &locale;
  StateStream stream(_IONBF);
  EXPECT_EQ(stream.try_set_orientation(LIBC_NAMESPACE::File::Orientation::WIDE),
            LIBC_NAMESPACE::File::Orientation::WIDE);
  EXPECT_EQ((*category)->references.load(), 2U);
  thread_locale = previous;
  release_locale_category(*category);
  ASSERT_EQ(stream.write(L"\u4e2d", 1).value, size_t(1));
  EXPECT_EQ(stream.length, size_t(7));
  ASSERT_EQ(stream.seek(0, SEEK_SET).value(), 0);
  wchar_t value;
  ASSERT_EQ(stream.read(&value, 1).value, size_t(1));
  EXPECT_EQ(value, wchar_t(0x4e2d));
}
#endif

TEST(LlvmLibcISO2022CNTest, PathPatternsDecodeShiftsBeforeMetacharacters) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  auto match = [&](const char *pattern, const char *input, int flags = 0) {
    return LIBC_NAMESPACE::internal::match_path_pattern(
        pattern, input, flags,
        LIBC_NAMESPACE::internal::RestartableConverter(*data), nullptr, nullptr);
  };
  const char *one = "\x1b$)A\x0e!!\x0f";
  EXPECT_TRUE(match("?", one).value());
  EXPECT_FALSE(match("??", one).value());
  EXPECT_TRUE(match("[\x1b$)A\x0e!!\x0f]", one).value());
  EXPECT_TRUE(match("*/?", "dir/\x1b$)A\x0e!!\x0f", FNM_PATHNAME).value());
  EXPECT_TRUE(match("*?", "\x1b$)A\x0e!!!\"\x0f").value());
  EXPECT_EQ(match("*", "\x1b$)A\x0e!").error(), EILSEQ);
  EXPECT_EQ(match("\x0e!!", one).error(), EILSEQ);
}

TEST(LlvmLibcISO2022CNTest, EncodingChoicesAndAtomicCapacity) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  ISO2022CNState state;
  EXPECT_TRUE(state.initial());
  encoded(state, *data, 0x4e2d, "\x1b$)A\x0e!!", 7);
  encoded(state, *data, 0x56fd, "\x1b$)G\"\"", 6);
  encoded(state, *data, 0x4e2d, "\"!", 2);
  encoded(state, *data, 'A',
          "\x0f"
          "A",
          2);
  EXPECT_FALSE(state.initial());
  encoded(state, *data, 0x4e2d, "\x0e\"!", 3);
  encoded(state, *data, 0x20000, "\x1b$*H\x1bN#!", 8);
  encoded(state, *data, 0x4e2d, "\"!", 2);
  encoded(state, *data, '\n', "\x0f\n", 2);
  EXPECT_TRUE(state.initial());
  encoded(state, *data, 0x4e2d, "\x1b$)A\x0e!!", 7);
  encoded(state, *data, 0, "\x0f\0", 2);
  EXPECT_TRUE(state.initial());
}

TEST(LlvmLibcISO2022CNTest, IncrementalDecodeAndCopies) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  const char wire[] = "\x1b$)A\x0e!!\x1b$*H\x1bN#!\"!";
  // Copy at every byte boundary, including designation and SS2 prefixes.
  for (size_t split = 0; split < sizeof(wire); ++split) {
    ISO2022CNState state;
    unsigned scalars = 0;
    const uint32_t expected[] = {0x4e2d, 0x20000};
    for (size_t i = 0; i + 3 < sizeof(wire); ++i) {
      if (i == split) {
        ISO2022CNState copy = state;
        state = copy;
      }
      auto step = state.push(uint8_t(wire[i]), *data);
      ASSERT_TRUE(step.has_value());
      if (step->event == Event::SCALAR) {
        ASSERT_LT(scalars, 2U);
        EXPECT_EQ(step->scalar, expected[scalars++]);
      }
    }
    EXPECT_EQ(scalars, 2U);
    EXPECT_TRUE(state.boundary());
    EXPECT_FALSE(state.initial());
    // SS2 did not change the GB locking shift.
    ASSERT_TRUE(state.push('!', *data).has_value());
    auto last = state.push('"', *data);
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->scalar, uint32_t(0x6587));
  }
}

TEST(LlvmLibcISO2022CNTest, StrictDesignationAndInvalidInput) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  ISO2022CNState state;
  EXPECT_EQ(state.push(0x0e, *data).error(), EILSEQ);
  EXPECT_TRUE(state.initial());
  ASSERT_TRUE(state.push(0x1b, *data).has_value());
  EXPECT_EQ(state.push('N', *data).error(), EILSEQ);
  EXPECT_EQ(state.push('O', *data).error(), EILSEQ);
  EXPECT_EQ(state.push(0, *data).error(), EILSEQ);
  EXPECT_FALSE(state.boundary());
  ASSERT_TRUE(state.push('$', *data).has_value());
  ASSERT_TRUE(state.push(')', *data).has_value());
  ASSERT_TRUE(state.push('A', *data).has_value());
  ASSERT_TRUE(state.push(0x0e, *data).has_value());
  EXPECT_EQ(state.push('\n', *data).error(), EILSEQ);
  ASSERT_TRUE(state.push('!', *data).has_value());
  EXPECT_EQ(state.push(0, *data).error(), EILSEQ);
  EXPECT_EQ(state.push(0x80, *data).error(), EILSEQ);
  EXPECT_EQ(state.push('~', *data).error(), EILSEQ);
  auto retry = state.push('!', *data);
  ASSERT_TRUE(retry.has_value());
  EXPECT_EQ(retry->scalar, uint32_t(0x4e2d));
}

TEST(LlvmLibcISO2022CNTest, ResetAndLineBoundaries) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  ISO2022CNState state;
  encoded(state, *data, 0x4e2d, "\x1b$)A\x0e!!", 7);
  uint8_t output = 0xcc;
  EXPECT_EQ(state.reset({&output, size_t(0)}).error(), E2BIG);
  EXPECT_EQ(output, uint8_t(0xcc));
  EXPECT_EQ(state.reset({&output, 1}).value(), size_t(1));
  EXPECT_EQ(output, uint8_t(0x0f));
  EXPECT_TRUE(state.initial());
  EXPECT_EQ(state.reset({}).value(), size_t(0));
  encoded(state, *data, 0x4e2d, "\x1b$)A\x0e!!", 7);
  ASSERT_TRUE(state.push(0x0f, *data).has_value());
  EXPECT_FALSE(state.initial());
  auto line = state.push('\n', *data);
  ASSERT_TRUE(line.has_value());
  EXPECT_EQ(line->scalar, uint32_t('\n'));
  EXPECT_TRUE(state.initial());
  ASSERT_TRUE(state.push(0x1b, *data).has_value());
  EXPECT_EQ(state.reset({&output, 1}).error(), EINVAL);
}

TEST(LlvmLibcISO2022CNTest, EncodingErrorsPreserveOutputAndState) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  ISO2022CNState state;
  const uint32_t invalid[] = {0x0e, 0x0f, 0x1b, 0xd800, 0x110000, 0x1234};
  for (uint32_t scalar : invalid) {
    uint8_t bytes[8]{};
    auto error = state.encode(scalar, bytes, *data);
    ASSERT_FALSE(error.has_value());
    EXPECT_EQ(error.error(), EILSEQ);
    for (uint8_t byte : bytes)
      EXPECT_EQ(byte, uint8_t(0));
    EXPECT_TRUE(state.initial());
  }
  ASSERT_TRUE(state.push(0x1b, *data).has_value());
  uint8_t bytes[8]{};
  EXPECT_EQ(state.encode('A', bytes, *data).error(), EINVAL);
}

TEST(LlvmLibcISO2022CNTest, PublicStateRoundTripAtEveryByte) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  const char wire[] = "\x1b$)A\x0e!!\x0f"
                      "A\x1b$)G\x0e\"!"
                      "\x1b$*H\x1bN#!\x0f\n";
  ISO2022CNState direct, restored;
  unsigned scalars = 0;
  const uint32_t expected[] = {0x4e2d, 'A', 0x4e2d, 0x20000, '\n', 0};
  for (char byte : wire) {
    auto saved = restored.save();
    auto loaded = ISO2022CNState::load(saved);
    ASSERT_TRUE(loaded.has_value());
    restored = *loaded;
    EXPECT_EQ(restored.initial(), direct.initial());
    EXPECT_EQ(restored.boundary(), direct.boundary());
    auto left = direct.push(uint8_t(byte), *data);
    auto right = restored.push(uint8_t(byte), *data);
    ASSERT_TRUE(left.has_value());
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(int(left->event), int(right->event));
    EXPECT_EQ(left->scalar, right->scalar);
    if (right->event == Event::SCALAR) {
      ASSERT_LT(scalars, 6U);
      EXPECT_EQ(right->scalar, expected[scalars++]);
    }
  }
  EXPECT_EQ(scalars, 6U);
  EXPECT_TRUE(restored.initial());
  auto saved = restored.save();
  EXPECT_EQ(uint32_t(saved.partial), uint32_t(0));
  EXPECT_EQ(saved.bytes_stored, uint8_t(0));
  EXPECT_EQ(saved.total_bytes, uint8_t(0));
}

TEST(LlvmLibcISO2022CNTest, PublicStateValidationAndIndependentCopies) {
  using LIBC_NAMESPACE::internal::mbstate;
  const mbstate invalid[] = {
      {1, 0, 0},       {0, 1, 1},       {0, 5, 5},       {0, 6, 6},
      {0, 0x80, 0},    {0, 0x88, 1},    {0, 0xff, 1},    {0, 0x80, 3},
      {0, 0x80, 16},   {0, 0x80, 8},    {1, 0x81, 0},    {0, 0x85, 9},
      {0x20, 0x85, 9}, {0x7f, 0x85, 9}, {0x21, 0x85, 1}, {0, 0x86, 0},
      {0x21, 0x87, 0}, {0x121, 0x87, 4}};
  for (const auto &saved : invalid) {
    auto result = ISO2022CNState::load(saved);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), EILSEQ);
  }
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  ISO2022CNState state;
  const char prefix[] = "\x1b$)A\x0e!";
  for (size_t i = 0; i + 1 < sizeof(prefix); ++i)
    ASSERT_TRUE(state.push(uint8_t(prefix[i]), *data).has_value());
  auto saved = state.save();
  auto other = ISO2022CNState::load(saved);
  ASSERT_TRUE(other.has_value());
  EXPECT_EQ(state.push('!', *data)->scalar, uint32_t(0x4e2d));
  EXPECT_EQ(other->push('"', *data)->scalar, uint32_t(0x6587));
  EXPECT_EQ(uint32_t(saved.partial), uint32_t('!'));
  auto boundary = ISO2022CNState::load(other->save());
  ASSERT_TRUE(boundary.has_value());
  encoded(*boundary, *data, 'A',
          "\x0f"
          "A",
          2);
  auto shifted_in = ISO2022CNState::load(boundary->save());
  ASSERT_TRUE(shifted_in.has_value());
  EXPECT_FALSE(shifted_in->initial());
  EXPECT_EQ(shifted_in->reset({}).value(), size_t(0));
  EXPECT_TRUE(shifted_in->initial());
}

TEST(LlvmLibcISO2022CNTest, RestartablePrefixAndPerCallCounts) {
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  const uint8_t wire[] = {0x1b, '$', '*', 'H', 0x1b, 'N', '#', '!'};
  for (size_t split = 0; split < sizeof(wire); ++split) {
    ISO2022CNState state;
    uint32_t scalar = 0xdead;
    EXPECT_EQ(state.decode(&scalar, {wire, split}, *data).value(), size_t(-2));
    EXPECT_EQ(scalar, uint32_t(0xdead));
    auto restored = ISO2022CNState::load(state.save());
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(
        restored->decode(&scalar, {wire + split, sizeof(wire) - split}, *data)
            .value(),
        sizeof(wire) - split);
    EXPECT_EQ(scalar, uint32_t(0x20000));
    EXPECT_FALSE(restored->initial());
  }
  ISO2022CNState state;
  const uint8_t designation[] = {0x1b, '$', ')', 'A'};
  // Complete shifts do not accumulate in either byte-sized saved field.
  for (unsigned i = 0; i != 300; ++i) {
    EXPECT_EQ(state.decode(nullptr, designation, *data).value(), size_t(-2));
    auto saved = state.save();
    EXPECT_EQ(saved.bytes_stored, uint8_t(0x80));
    EXPECT_EQ(saved.total_bytes, uint8_t(1));
    auto restored = ISO2022CNState::load(saved);
    ASSERT_TRUE(restored.has_value());
    state = *restored;
  }
  const uint8_t rest[] = {0x0e, '!', '!', 0};
  uint32_t scalar = 0;
  EXPECT_EQ(state.decode(&scalar, rest, *data).value(), size_t(3));
  EXPECT_EQ(scalar, uint32_t(0x4e2d));
  EXPECT_EQ(state.decode(&scalar, {rest + 3, 1}, *data).value(), size_t(0));
  EXPECT_EQ(scalar, uint32_t(0));
  EXPECT_TRUE(state.initial());
  const uint8_t escape[] = {0x1b};
  EXPECT_EQ(state.decode(&scalar, escape, *data).value(), size_t(-2));
  scalar = 0xdead;
  EXPECT_EQ(state.decode(&scalar, {rest + 3, 1}, *data).error(), EILSEQ);
  EXPECT_EQ(scalar, uint32_t(0xdead));
}

TEST(LlvmLibcISO2022CNTest, RestartableConversionUsesOnlySavedFields) {
  using LIBC_NAMESPACE::internal::mbstate;
  using LIBC_NAMESPACE::internal::RestartableConverter;
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  RestartableConverter converter(*data);
  mbstate state{};
  EXPECT_TRUE(converter.valid(state));
  EXPECT_TRUE(converter.initial(state));
  uint8_t output[8];
  for (auto &byte : output)
    byte = 0xcc;
  EXPECT_EQ(converter.encode(0x20000, {output, 7}, state).error(), E2BIG);
  EXPECT_TRUE(converter.initial(state));
  for (auto byte : output)
    EXPECT_EQ(byte, uint8_t(0xcc));
  EXPECT_EQ(converter.encode(0x20000, output, state).value(), size_t(8));
  EXPECT_FALSE(converter.initial(state));
  mbstate input{};
  uint32_t scalar = 0xdead;
  for (size_t i = 0; i != sizeof(output); ++i) {
    auto result = converter.decode(&scalar, {output + i, 1}, input);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, i == 7 ? size_t(1) : size_t(-2));
    EXPECT_TRUE(converter.valid(input));
    // Recreate both the descriptor and state between invocations.
    RestartableConverter fresh(*data);
    mbstate copy = input;
    EXPECT_TRUE(fresh.valid(copy));
    input = copy;
  }
  EXPECT_EQ(scalar, uint32_t(0x20000));
  EXPECT_EQ(converter.encode(0, output, state).value(), size_t(1));
  EXPECT_TRUE(converter.initial(state));
  EXPECT_EQ(converter.decode(&scalar, {output, 1}, input).value(), size_t(0));
  EXPECT_TRUE(converter.initial(input));

  const uint8_t partial[] = {0x1b, '$'};
  EXPECT_EQ(converter.decode(&scalar, partial, input).value(), size_t(-2));
  EXPECT_EQ(converter.encode('x', output, input).error(), EINVAL);
  const uint8_t nul[] = {0};
  EXPECT_EQ(converter.decode(&scalar, nul, input).error(), EILSEQ);
}

TEST(LlvmLibcISO2022CNTest, RestartableStatelessEncodings) {
  using LIBC_NAMESPACE::internal::mbstate;
  using LIBC_NAMESPACE::internal::RestartableConverter;
  RestartableConverter utf8(true), latin1(false);
  mbstate state{};
  uint8_t output[8];
  for (uint32_t value = 0; value != 256; ++value) {
    EXPECT_EQ(latin1.encode(value, output, state).value(), size_t(1));
    EXPECT_EQ(output[0], uint8_t(value));
    uint32_t scalar = 0xdead;
    EXPECT_EQ(latin1.decode(&scalar, {output, 1}, state).value(),
              value == 0 ? size_t(0) : size_t(1));
    EXPECT_EQ(scalar, value);
    EXPECT_TRUE(latin1.initial(state));
  }
  EXPECT_EQ(latin1.encode(256, output, state).error(), EILSEQ);
  EXPECT_EQ(utf8.encode(0x20000, output, state).value(), size_t(4));
  uint32_t scalar = 0xdead;
  EXPECT_EQ(utf8.decode(&scalar, {output, 2}, state).value(), size_t(-2));
  EXPECT_FALSE(utf8.initial(state));
  EXPECT_FALSE(latin1.valid(state));
  auto copy = state;
  EXPECT_EQ(utf8.decode(&scalar, {output + 2, 2}, copy).value(), size_t(2));
  EXPECT_EQ(scalar, uint32_t(0x20000));
  EXPECT_TRUE(utf8.initial(copy));
  const uint8_t nul[] = {0};
  EXPECT_EQ(utf8.decode(&scalar, nul, state).error(), EILSEQ);
  const mbstate invalid[] = {{0, 0, 1},    {0, 1, 2},     {0x20, 1, 2},
                             {0x10, 1, 3}, {0x360, 2, 3}, {5, 1, 4},
                             {0xf, 2, 4},  {0x110, 2, 4}, {0x4400, 3, 4}};
  for (const auto &bad : invalid)
    EXPECT_FALSE(utf8.valid(bad));
}

#ifdef LIBC_FULL_BUILD
TEST(LlvmLibcISO2022CNTest, PublicRestartableStateAndImplicitIsolation) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  // This fixture admits only the conversion view, not a public regional name
  // or a complete locale. Real generated-table qualification is separate.
  __locale_data category("codec-test", false);
  category.chinese_encoding = *data;
  auto locale = global_locale_snapshot();
  locale.__data[LC_CTYPE] = &category;
  struct Selection {
    locale_t previous = thread_locale;
    explicit Selection(locale_t selected) { thread_locale = selected; }
    ~Selection() { thread_locale = previous; }
  } selection(&locale);

  const char wire[] = "\x1b$*H\x1bN#!";
  for (size_t split = 0; split != 8; ++split) {
    struct State {
      unsigned before = 0x12345678;
      mbstate_t state{};
      unsigned after = 0xabcdef;
    } first;
    wchar_t scalar = 0x1234;
    EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&scalar, wire, split, &first.state),
              size_t(-2));
    EXPECT_EQ(scalar, wchar_t(0x1234));
    auto second = first;
    EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&scalar, wire + split, 8 - split,
                                      &second.state),
              size_t(8 - split));
    EXPECT_EQ(scalar, wchar_t(0x20000));
    EXPECT_EQ(LIBC_NAMESPACE::mbsinit(&second.state), 0);
    EXPECT_EQ(LIBC_NAMESPACE::mbrlen("", 1, &second.state), size_t(0));
    EXPECT_NE(LIBC_NAMESPACE::mbsinit(&second.state), 0);
    EXPECT_EQ(second.before, first.before);
    EXPECT_EQ(second.after, first.after);
  }
  mbstate_t state{};
  char output[10];
  for (char &byte : output)
    byte = 'x';
  EXPECT_EQ(LIBC_NAMESPACE::wcrtomb(output + 1, 0x20000, &state), size_t(8));
  for (size_t i = 0; i != 8; ++i)
    EXPECT_EQ(output[i + 1], wire[i]);
  EXPECT_EQ(output[0], 'x');
  EXPECT_EQ(output[9], 'x');
  EXPECT_EQ(LIBC_NAMESPACE::mbsinit(&state), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wcrtomb(nullptr, 0x1234, &state), size_t(1));
  EXPECT_NE(LIBC_NAMESPACE::mbsinit(&state), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wcrtomb(output, 0x4e2d, &state), size_t(7));
  EXPECT_EQ(LIBC_NAMESPACE::wcrtomb(nullptr, 0, &state), size_t(2));
  EXPECT_NE(LIBC_NAMESPACE::mbsinit(&state), 0);

  wchar_t scalar = 0x1234;
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&scalar, wire, 3, nullptr), size_t(-2));
  EXPECT_EQ(LIBC_NAMESPACE::mbrlen("A", 1, nullptr), size_t(1));
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&scalar, wire + 3, 5, nullptr), size_t(5));
  EXPECT_EQ(scalar, wchar_t(0x20000));
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(nullptr, nullptr, 0, nullptr), size_t(0));
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&scalar, wire, 1, &state), size_t(-2));
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&scalar, nullptr, 0, &state), size_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
}

TEST(LlvmLibcISO2022CNTest, PublicUTF16PreservesPersistentState) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  __locale_data category("codec-test", false);
  category.chinese_encoding = *data;
  auto locale = global_locale_snapshot();
  locale.__data[LC_CTYPE] = &category;
  struct Selection {
    locale_t previous = thread_locale;
    explicit Selection(locale_t selected) { thread_locale = selected; }
    ~Selection() { thread_locale = previous; }
  } selection(&locale);
  mbstate_t input{}, output{};
  char bytes[8];
  // Retain a G1 locking shift while emitting a supplementary G2 character.
  ASSERT_EQ(LIBC_NAMESPACE::c32rtomb(bytes, char32_t(0x4e2d), &output),
            size_t(7));
  char32_t scalar = 0;
  ASSERT_EQ(LIBC_NAMESPACE::mbrtoc32(&scalar, bytes, 7, &input), size_t(7));
  EXPECT_EQ(uint32_t(scalar), uint32_t(0x4e2d));
  ASSERT_EQ(LIBC_NAMESPACE::c16rtomb(bytes, char16_t(0xd840), &output),
            size_t(0));
  EXPECT_EQ(LIBC_NAMESPACE::mbsinit(&output), 0);
  auto copied_output = output;
  ASSERT_EQ(LIBC_NAMESPACE::c16rtomb(bytes, char16_t(0xdc00), &copied_output),
            size_t(8));
  char16_t unit = 0;
  ASSERT_EQ(LIBC_NAMESPACE::mbrtoc16(&unit, bytes, 8, &input), size_t(8));
  EXPECT_EQ(unsigned(unit), 0xd840U);
  EXPECT_EQ(LIBC_NAMESPACE::mbsinit(&input), 0);
  auto copied_input = input;
  ASSERT_EQ(LIBC_NAMESPACE::mbrtoc16(&unit, "", 0, &copied_input), size_t(-3));
  EXPECT_EQ(unsigned(unit), 0xdc00U);
  EXPECT_EQ(LIBC_NAMESPACE::mbsinit(&copied_input), 0);
  // No new GB designation or SO is necessary after the second UTF-16 unit.
  ASSERT_EQ(LIBC_NAMESPACE::c32rtomb(bytes, char32_t(0x4e2d), &copied_output),
            size_t(2));
  EXPECT_EQ(bytes[0], '!');
  EXPECT_EQ(bytes[1], '!');
  ASSERT_EQ(LIBC_NAMESPACE::mbrtoc32(&scalar, bytes, 2, &copied_input),
            size_t(2));
  EXPECT_EQ(uint32_t(scalar), uint32_t(0x4e2d));
  ASSERT_EQ(LIBC_NAMESPACE::c16rtomb(nullptr, 0, &copied_output), size_t(2));
  EXPECT_NE(LIBC_NAMESPACE::mbsinit(&copied_output), 0);
  ASSERT_EQ(LIBC_NAMESPACE::mbrtoc16(nullptr, nullptr, 0, &copied_input),
            size_t(0));
  EXPECT_NE(LIBC_NAMESPACE::mbsinit(&copied_input), 0);

  // A pending high surrogate cannot be discarded by the null-output form.
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::c16rtomb(nullptr, 0, &output), size_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
  mbstate_t bad{};
  EXPECT_EQ(LIBC_NAMESPACE::c16rtomb(bytes, char16_t(0xdc00), &bad),
            size_t(-1));
}

TEST(LlvmLibcISO2022CNTest, PublicUTF16StatelessRegression) {
  using namespace LIBC_NAMESPACE::internal;
  auto locale = locale_from_mask(LOCALE_MASK);
  struct Selection {
    locale_t previous = thread_locale;
    explicit Selection(locale_t selected) { thread_locale = selected; }
    ~Selection() { thread_locale = previous; }
  } selection(&locale);
  mbstate_t state{};
  char16_t unit = 123;
  const char bytes[] = "\xf0\x9f\x98\x80";
  EXPECT_EQ(LIBC_NAMESPACE::mbrtoc16(&unit, bytes, 2, &state), size_t(-2));
  EXPECT_EQ(unsigned(unit), 123U);
  EXPECT_EQ(LIBC_NAMESPACE::mbrtoc16(&unit, bytes + 2, 2, &state), size_t(2));
  EXPECT_EQ(unsigned(unit), 0xd83dU);
  EXPECT_EQ(LIBC_NAMESPACE::mbrtoc16(&unit, "A", 1, &state), size_t(-3));
  EXPECT_EQ(unsigned(unit), 0xde00U);
  EXPECT_NE(LIBC_NAMESPACE::mbsinit(&state), 0);
  char output[8];
  EXPECT_EQ(LIBC_NAMESPACE::c16rtomb(output, char16_t(0xd83d), &state),
            size_t(0));
  EXPECT_EQ(LIBC_NAMESPACE::c16rtomb(output, char16_t(0xde00), &state),
            size_t(4));
  for (unsigned i = 0; i != 4; ++i)
    EXPECT_EQ(output[i], bytes[i]);
  EXPECT_NE(LIBC_NAMESPACE::mbsinit(&state), 0);
}

TEST(LlvmLibcISO2022CNTest, PublicStringsPreserveShiftAndProgress) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  __locale_data category("codec-test", false);
  category.chinese_encoding = *data;
  auto locale = global_locale_snapshot();
  locale.__data[LC_CTYPE] = &category;
  struct Selection {
    locale_t previous = thread_locale;
    explicit Selection(locale_t selected) { thread_locale = selected; }
    ~Selection() { thread_locale = previous; }
  } selection(&locale);
  const char wire[] = "\x1b$)A\x0e!!\x1b$*H\x1bN#!\x0f\0";
  const wchar_t expected[] = {0x4e2d, 0x20000, 0};
  // Split inside every designation, shift and character pair.
  for (size_t split = 0; split != sizeof(wire) - 1; ++split) {
    const char *source = wire;
    mbstate_t state{};
    wchar_t output[] = {123, 123, 123, 123};
    size_t first =
        LIBC_NAMESPACE::mbsnrtowcs(output, &source, split, 3, &state);
    ASSERT_LE(first, size_t(2));
    EXPECT_EQ(source, wire + split);
    auto copied = state;
    ASSERT_EQ(
        LIBC_NAMESPACE::mbsrtowcs(output + first, &source, 3 - first, &copied),
        size_t(2) - first);
    EXPECT_EQ(source, nullptr);
    for (size_t i = 0; i != 3; ++i)
      EXPECT_EQ(output[i], expected[i]);
    EXPECT_EQ(output[3], wchar_t(123));
    EXPECT_NE(LIBC_NAMESPACE::mbsinit(&copied), 0);
  }
  const char *source = wire;
  mbstate_t state{};
  EXPECT_EQ(LIBC_NAMESPACE::mbsrtowcs(nullptr, &source, 0, &state), size_t(2));
  EXPECT_EQ(source, wire);
  EXPECT_NE(LIBC_NAMESPACE::mbsinit(&state), 0);
  const char bad[] = "\x1b$)A\x0e!!\x80";
  source = bad;
  wchar_t output[] = {123, 123, 123};
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::mbsrtowcs(output, &source, 3, &state), size_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
  EXPECT_EQ(source, bad + 7);
  EXPECT_EQ(output[0], wchar_t(0x4e2d));
  EXPECT_EQ(output[1], wchar_t(123));
}

TEST(LlvmLibcISO2022CNTest, PublicStringOutputIsCharacterAtomic) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  __locale_data category("codec-test", false);
  category.chinese_encoding = *data;
  auto locale = global_locale_snapshot();
  locale.__data[LC_CTYPE] = &category;
  struct Selection {
    locale_t previous = thread_locale;
    explicit Selection(locale_t selected) { thread_locale = selected; }
    ~Selection() { thread_locale = previous; }
  } selection(&locale);
  const wchar_t input[] = {0x4e2d, 0x20000, 0};
  const char wire[] = "\x1b$)A\x0e!!\x1b$*H\x1bN#!\x0f\0";
  for (size_t capacity = 0; capacity != sizeof(wire); ++capacity) {
    char output[20];
    for (char &byte : output)
      byte = 'x';
    const wchar_t *source = input;
    mbstate_t state{};
    size_t result =
        LIBC_NAMESPACE::wcsrtombs(output + 1, &source, capacity, &state);
    size_t written = capacity < 7    ? 0
                     : capacity < 15 ? 7
                     : capacity < 17 ? 15
                                     : 17;
    EXPECT_EQ(result, written == 17 ? size_t(16) : written);
    EXPECT_EQ(source, written == 17 ? nullptr
                                    : input + (written == 0   ? 0
                                               : written == 7 ? 1
                                                              : 2));
    for (size_t i = 0; i != written; ++i)
      EXPECT_EQ(output[1 + i], wire[i]);
    EXPECT_EQ(output[0], 'x');
    for (size_t i = 1 + written; i != sizeof(output); ++i)
      EXPECT_EQ(output[i], 'x');
    if (written == 0 || written == 17)
      EXPECT_NE(LIBC_NAMESPACE::mbsinit(&state), 0);
  }
  const wchar_t *source = input;
  mbstate_t state{};
  char output[20];
  EXPECT_EQ(LIBC_NAMESPACE::wcsnrtombs(output, &source, 1, 20, &state),
            size_t(7));
  EXPECT_EQ(source, input + 1);
  auto saved = state;
  EXPECT_EQ(LIBC_NAMESPACE::wcsrtombs(nullptr, &source, 0, &state), size_t(9));
  EXPECT_EQ(source, input + 1);
  EXPECT_EQ(LIBC_NAMESPACE::wcsrtombs(output, &source, 20, &state), size_t(9));
  EXPECT_EQ(source, nullptr);
  EXPECT_NE(LIBC_NAMESPACE::mbsinit(&state), 0);
  const wchar_t bad[] = {0x4e2d, 0xd800, 0};
  source = bad;
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::wcsrtombs(output, &source, 20, &saved), size_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
  EXPECT_EQ(source, bad + 1);
}

TEST(LlvmLibcISO2022CNTest, LegacyConversionAndSingleByteInitialState) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture fixture;
  auto data = fixture.read();
  ASSERT_TRUE(data.has_value());
  __locale_data category("codec-test", false);
  category.chinese_encoding = *data;
  auto locale = global_locale_snapshot();
  locale.__data[LC_CTYPE] = &category;
  struct Selection {
    locale_t previous = thread_locale;
    explicit Selection(locale_t selected) { thread_locale = selected; }
    ~Selection() { thread_locale = previous; }
  } selection(&locale);
  EXPECT_EQ(LIBC_NAMESPACE::__ctype_get_mb_cur_max(), size_t(8));
  EXPECT_NE(LIBC_NAMESPACE::mblen(nullptr, 0), 0);
  EXPECT_NE(LIBC_NAMESPACE::mbtowc(nullptr, nullptr, 0), 0);
  EXPECT_NE(LIBC_NAMESPACE::wctomb(nullptr, 0), 0);
  const char wire[] = "\x1b$)A\x0e!!\x0f\0";
  wchar_t scalar = 123;
  EXPECT_EQ(LIBC_NAMESPACE::mbtowc(&scalar, wire, 7), 7);
  EXPECT_EQ(scalar, wchar_t(0x4e2d));
  EXPECT_EQ(LIBC_NAMESPACE::mblen("!!", 2), 1);
  EXPECT_EQ(LIBC_NAMESPACE::mbtowc(&scalar, "!!", 2), 2);
  EXPECT_EQ(scalar, wchar_t(0x4e2d));
  EXPECT_NE(LIBC_NAMESPACE::mbtowc(nullptr, nullptr, 0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::mbtowc(&scalar, "!!", 2), 1);
  EXPECT_EQ(scalar, wchar_t('!'));
  char output[10]{};
  EXPECT_EQ(LIBC_NAMESPACE::wctomb(output, 0x4e2d), 7);
  EXPECT_EQ(LIBC_NAMESPACE::wctomb(output, 0x4e2d), 2);
  EXPECT_NE(LIBC_NAMESPACE::wctomb(nullptr, 0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wctomb(output, 0x4e2d), 7);
  EXPECT_EQ(LIBC_NAMESPACE::wctomb(output, 0), 2);
  EXPECT_EQ(output[0], char(0x0f));
  EXPECT_EQ(output[1], char(0));
  wchar_t wide[3]{};
  EXPECT_EQ(LIBC_NAMESPACE::mbstowcs(wide, wire, 3), size_t(1));
  EXPECT_EQ(wide[0], wchar_t(0x4e2d));
  EXPECT_EQ(wide[1], wchar_t(0));
  EXPECT_EQ(LIBC_NAMESPACE::wcstombs(output, wide, 10), size_t(8));
  for (size_t i = 0; i != 9; ++i)
    EXPECT_EQ(output[i], wire[i]);
  LIBC_NAMESPACE::libc_errno = EBUSY;
  EXPECT_EQ(LIBC_NAMESPACE::btowc('A'), wint_t('A'));
  EXPECT_EQ(LIBC_NAMESPACE::btowc(0), wint_t(0));
  EXPECT_EQ(LIBC_NAMESPACE::btowc(0x1b), WEOF);
  EXPECT_EQ(LIBC_NAMESPACE::btowc(0x0e), WEOF);
  EXPECT_EQ(LIBC_NAMESPACE::btowc(0x80), WEOF);
  EXPECT_EQ(LIBC_NAMESPACE::wctob(wint_t('A')), int('A'));
  EXPECT_EQ(LIBC_NAMESPACE::wctob(0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::wctob(0x4e2d), -1);
  EXPECT_EQ(LIBC_NAMESPACE::wctob(0x1b), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBUSY);
}
#endif
