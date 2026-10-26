//===-- Internal word expansion evaluation --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/expand.h"
#include "hdr/errno_macros.h"
#include "src/__support/integer_to_string.h"
#include "src/wordexp/dollar_quote.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
using Scalars = cpp::span<const uint32_t>;
using Kind = WordToken::Kind;
using Action = ParameterExpansion::Action;
using Failure = WordExpansionError;

int append(CharVector &out, cpp::string_view value) {
  for (char c : value)
    if (!out.append(c))
      return ENOMEM;
  return 0;
}

class Encoder {
  RestartableConverter converter;
  mbstate state{};
  CharVector &output;

public:
  Encoder(RestartableConverter converter, CharVector &output)
      : converter(converter), output(output) {}
  int character(uint32_t scalar) {
    uint8_t bytes[MAX_LOCALE_CHARACTER_BYTES];
    auto length = converter.encode(scalar, bytes, state);
    if (!length)
      return length.error();
    return append(output, {reinterpret_cast<const char *>(bytes),
                           *length - (scalar == 0)});
  }
  int raw(uint32_t byte) { return output.append(char(byte)) ? 0 : ENOMEM; }
  static int quoted(void *p, QuotedUnit unit) {
    auto &self = *static_cast<Encoder *>(p);
    return unit.kind == QuotedUnit::Kind::BYTE ? self.raw(unit.value)
                                               : self.character(unit.value);
  }
};

int encode(Scalars value, RestartableConverter converter, CharVector &output) {
  Encoder encoder(converter, output);
  for (auto scalar : value)
    if (int error = encoder.character(scalar))
      return error;
  return encoder.character(0);
}

bool name_start(uint32_t c) {
  return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool name_part(uint32_t c) { return name_start(c) || (c >= '0' && c <= '9'); }

size_t skip_continuations(Scalars input, size_t at) {
  while (at < input.size() && input[at] == '\\' && input.size() - at > 1 &&
         input[at + 1] == '\n')
    at += 2;
  return at;
}

int name(Scalars input, CharVector &out) {
  for (size_t i = 0; i < input.size();) {
    i = skip_continuations(input, i);
    if (i == input.size())
      break;
    if (input[i] > 127)
      return EINVAL;
    if (!out.append(char(input[i++])))
      return ENOMEM;
  }
  return 0;
}

int pattern_text(const WordFragments &word, RestartableConverter converter,
                 CharVector &output) {
  for (const auto &part : word.parts()) {
    if (!part.quoted) {
      if (int error = append(output, part.bytes))
        return error;
      continue;
    }
    mbstate state{};
    Encoder encoder(converter, output);
    auto bytes = part.bytes;
    while (!bytes.empty()) {
      uint32_t scalar;
      auto read = converter.decode(
          &scalar,
          {reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()},
          state);
      if (!read)
        return read.error();
      if (*read == size_t(-2)) {
        if (!converter.boundary(state))
          return EILSEQ;
        break;
      }
      if (!*read || *read > bytes.size() || !scalar)
        return EILSEQ;
      if (scalar == '\\' || scalar == '*' || scalar == '?' || scalar == '[' ||
          scalar == ']')
        if (int error = encoder.character('\\'))
          return error;
      if (int error = encoder.character(scalar))
        return error;
      bytes.remove_prefix(*read);
    }
    if (int error = encoder.character(0))
      return error;
  }
  return 0;
}

class Evaluator {
  Scalars input;
  bool no_command, undef;
  RestartableConverter converter;
  const __locale_data *ctype;
  const CollationData *collation;
  const WordExpansionServices &services;
  WordExpansionSink sink;
  WordFragments &output;
  unsigned depth;
  WordExpansionResult result;

  int fail(Failure error, int detail = 0) {
    result = {error, {}, detail};
    return detail ? detail : EINVAL;
  }
  int system(int error) { return error ? fail(Failure::SYSTEM, error) : 0; }
  int syntax(WordSyntaxResult error) {
    if (error)
      return 0;
    if (error.error == WordSyntaxError::RESOURCE)
      return system(error.sink_error);
    if (result)
      result = {Failure::SYNTAX, error, 0};
    return EINVAL;
  }
  int nested(Scalars text, bool quoted, bool arithmetic, WordFragments &word) {
    if (depth == 128)
      return syntax({WordSyntaxError::NESTING_LIMIT});
    Evaluator child(text, no_command, undef, converter, ctype, collation,
                    services, {}, word, depth + 1);
    auto done = child.fragment(quoted, arithmetic);
    if (!done) {
      result = done;
      return EINVAL;
    }
    return 0;
  }
  int value(cpp::string_view bytes, bool quoted) {
    return system(output.append(bytes, true, quoted));
  }
  int lookup(cpp::string_view variable, CharVector &owned, bool &set) {
    if (!services.lookup)
      return fail(Failure::SERVICE_REQUIRED);
    auto read = services.lookup(services.context, variable);
    if (!read)
      return system(read.error());
    set = read->set;
    return system(set ? append(owned, read->value) : 0);
  }
  int parameter(Scalars text, bool quoted, bool braced) {
    CharVector header;
    size_t at = skip_continuations(text, 0);
    if (braced && at < text.size() && text[at] == '#') {
      if (!header.append('#'))
        return system(ENOMEM);
      at = skip_continuations(text, at + 1);
    }
    if (at == text.size() || !name_start(text[at]))
      return syntax({WordSyntaxError::SYNTAX});
    do {
      if (!header.append(char(text[at])))
        return system(ENOMEM);
      at = skip_continuations(text, at + 1);
    } while (at < text.size() && name_part(text[at]));
    if (at < text.size()) {
      if (text[at] == ':') {
        if (!header.append(':'))
          return system(ENOMEM);
        at = skip_continuations(text, at + 1);
      }
      if (at == text.size())
        return syntax({WordSyntaxError::SYNTAX});
      uint32_t op = text[at];
      if (op > 127 || !header.append(char(op)))
        return system(op > 127 ? EINVAL : ENOMEM);
      at = skip_continuations(text, at + 1);
      if ((op == '#' || op == '%') && at < text.size() && text[at] == op) {
        if (!header.append(char(op)))
          return system(ENOMEM);
        at = skip_continuations(text, at + 1);
      }
    }
    auto parsed = parse_word_parameter({header.c_str(), header.length()});
    if (!parsed)
      return syntax({WordSyntaxError::SYNTAX});
    bool set;
    CharVector stored;
    if (int error = lookup(parsed->name, stored, set))
      return error;
    cpp::string_view bytes(stored.c_str(), stored.length());
    Action action = parsed->select(set, bytes.empty());
    if (!set && undef &&
        (action == Action::VALUE || action == Action::LENGTH ||
         action == Action::PATTERN))
      return fail(Failure::UNDEFINED);
    if (action == Action::VALUE)
      return value(bytes, quoted);
    if (action == Action::EMPTY)
      return value({}, quoted);
    if (action == Action::LENGTH) {
      auto length = word_parameter_length(bytes, converter);
      if (!length)
        return system(length.error());
      const IntegerToString<size_t> count(*length);
      return value(count.view(), quoted);
    }
    WordFragments word;
    if (int error =
            nested(text.subspan(at), action == Action::PATTERN ? false : quoted,
                   false, word))
      return error;
    if (action == Action::PATTERN) {
      CharVector pattern, remaining;
      if (int error = pattern_text(word, converter, pattern))
        return system(error);
      auto removed = remove_word_pattern({pattern.c_str(), pattern.length()},
                                         bytes, parsed->operation, converter,
                                         ctype, collation, remaining);
      if (!removed)
        return system(removed.error());
      return value({remaining.c_str(), remaining.length()}, quoted);
    }
    if (action == Action::ASSIGN_WORD || action == Action::ERROR_WORD) {
      CharVector flattened;
      if (int error = word.flatten(flattened))
        return system(error);
      cpp::string_view expanded(flattened.c_str(), flattened.length());
      if (action == Action::ERROR_WORD) {
        if (!services.diagnostic)
          return fail(Failure::SERVICE_REQUIRED);
        if (int error =
                services.diagnostic(services.context, parsed->name, expanded))
          return system(error);
        return fail(Failure::PARAMETER_ERROR);
      }
      if (!services.assign)
        return fail(Failure::SERVICE_REQUIRED);
      if (int error = services.assign(services.context, parsed->name, expanded))
        return system(error);
    }
    // The selected word is itself a parameter expansion. Unquoted literal
    // bytes in it are eligible for splitting; explicitly quoted bytes are not.
    for (const auto &part : word.parts())
      if (int error = value(part.bytes, quoted || part.quoted))
        return error;
    return 0;
  }
  int token(WordToken token) {
    auto text = input.subspan(token.begin, token.end - token.begin);
    CharVector bytes;
    switch (token.kind) {
    case Kind::END_WORD:
      if (!sink.word)
        return fail(Failure::SERVICE_REQUIRED);
      if (int error = sink.word(sink.context, output.parts()))
        return system(error);
      output.clear();
      return 0;
    case Kind::LITERAL:
      if (int error = encode(text, converter, bytes))
        return system(error);
      return system(
          output.append({bytes.c_str(), bytes.length()}, false, token.quoted));
    case Kind::DOLLAR_QUOTED: {
      Encoder encoder(converter, bytes);
      auto done = decode_dollar_quote(text, {&encoder, Encoder::quoted});
      if (!done)
        return done.error == WordSyntaxError::SINK_ERROR
                   ? system(done.sink_error)
                   : syntax(done);
      if (int error = encoder.character(0))
        return system(error);
      return system(
          output.append({bytes.c_str(), bytes.length()}, false, true));
    }
    case Kind::PARAMETER:
    case Kind::BRACED_PARAMETER:
      return parameter(text, token.quoted,
                       token.kind == Kind::BRACED_PARAMETER);
    case Kind::TILDE: {
      CharVector user;
      if (int error = name(text, user))
        return system(error);
      if (!services.home)
        return fail(Failure::SERVICE_REQUIRED);
      auto home =
          services.home(services.context, {user.c_str(), user.length()});
      if (!home)
        return system(home.error());
      if (home->set)
        return system(output.append(home->value, false, true));
      // Unknown login names/HOME have unspecified tilde results. Preserve the
      // original spelling rather than inventing a directory.
      if (int error = output.append("~", false, false))
        return system(error);
      return system(output.append({user.c_str(), user.length()}, false, false));
    }
    case Kind::ARITHMETIC: {
      WordFragments expanded;
      if (int error = nested(text, true, true, expanded))
        return error;
      if (int error = expanded.flatten(bytes))
        return system(error);
      auto number = evaluate_wordexp_arithmetic({bytes.c_str(), bytes.length()},
                                                services.arithmetic);
      if (!number)
        return system(number.error());
      const IntegerToString<long> digits(*number);
      return value(digits.view(), token.quoted);
    }
    case Kind::COMMAND:
    case Kind::BACKQUOTED_COMMAND: {
      if (!services.command)
        return fail(Failure::SERVICE_REQUIRED);
      if (int error =
              services.command(services.context, text,
                               token.kind == Kind::BACKQUOTED_COMMAND, bytes))
        return system(error);
      size_t length = 0;
      char *data = bytes.c_str();
      // Preserve capture separately from shell normalization: embedded NULs
      // are discarded without truncating the bytes that follow them.
      for (size_t i = 0; i < bytes.length(); ++i)
        if (data[i])
          data[length++] = data[i];
      while (length && data[length - 1] == '\n')
        --length;
      return value({data, length}, token.quoted);
    }
    }
    __builtin_unreachable();
  }
  static int emit(void *p, WordToken token) {
    return static_cast<Evaluator *>(p)->token(token);
  }

public:
  Evaluator(Scalars input, bool no_command, bool undef,
            RestartableConverter converter, const __locale_data *ctype,
            const CollationData *collation,
            const WordExpansionServices &services, WordExpansionSink sink,
            WordFragments &output, unsigned depth = 0)
      : input(input), no_command(no_command), undef(undef),
        converter(converter), ctype(ctype), collation(collation),
        services(services), sink(sink), output(output), depth(depth) {}
  WordExpansionResult run() {
    syntax(
        parse_word_syntax(input, no_command, {this, emit}, services.commands));
    return result;
  }
  WordExpansionResult fragment(bool quoted, bool arithmetic) {
    syntax(parse_word_fragment(input, quoted, arithmetic, no_command,
                               {this, emit}, services.commands));
    return result;
  }
};
} // namespace

WordExpansionResult expand_word_fragments(
    Scalars input, bool no_command, bool undef, RestartableConverter converter,
    const __locale_data *ctype, const CollationData *collation,
    WordExpansionServices services, WordExpansionSink sink) {
  WordFragments word;
  return Evaluator(input, no_command, undef, converter, ctype, collation,
                   services, sink, word)
      .run();
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
