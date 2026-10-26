//===-- Word expansion lexical structure ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/syntax.h"
#include "src/wordexp/dollar_quote.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
using Kind = WordToken::Kind;
using Error = WordSyntaxError;

bool name_start(uint32_t c) {
  return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool name_part(uint32_t c) { return name_start(c) || (c >= '0' && c <= '9'); }

class Parser {
  cpp::span<const uint32_t> input;
  bool no_command;
  WordSyntaxSink sink;
  WordCommandSyntax commands;
  size_t at = 0;
  WordSyntaxResult result;
  enum class Mode {
    TOP, PARAMETER, ARITHMETIC, DOUBLE_QUOTE, FRAGMENT, ARITHMETIC_FRAGMENT
  };

  size_t skip_continuations(size_t from) const {
    while (from < input.size() && input[from] == '\\' &&
           input.size() - from > 1 && input[from + 1] == '\n')
      from += 2;
    return from;
  }
  void advance() { at = skip_continuations(at + 1); }
  uint32_t peek(size_t n = 0) const {
    size_t position = at;
    while (n-- && position < input.size())
      position = skip_continuations(position + 1);
    return position < input.size() ? input[position] : 0;
  }
  bool fail(Error error) {
    if (result)
      result = {error, at};
    return false;
  }
  bool emit(Kind kind, size_t begin, size_t end, bool quoted, bool output) {
    if (output && sink.emit) {
      int error = sink.emit(sink.context, {kind, begin, end, quoted});
      if (error) {
        result = {Error::SINK_ERROR, begin, error};
        return false;
      }
    }
    return true;
  }
  bool single_quote(bool dollar, bool output) {
    size_t begin = ++at;
    while (at != input.size() && peek() != '\'') {
      if (!peek())
        return fail(Error::SYNTAX);
      if (dollar && peek() == '\\') {
        if (++at == input.size())
          return fail(Error::SYNTAX);
      }
      ++at;
    }
    if (at == input.size())
      return fail(Error::SYNTAX);
    size_t end = at++;
    if (dollar) {
      auto checked = decode_dollar_quote(input.subspan(begin, end - begin));
      if (!checked) {
        checked.position += begin;
        result = checked;
        return false;
      }
    }
    return emit(dollar ? Kind::DOLLAR_QUOTED : Kind::LITERAL, begin, end, true,
                output);
  }
  bool command(bool legacy, bool quoted, bool output) {
    if (no_command)
      return fail(Error::COMMAND_FORBIDDEN);
    if (!commands.scan)
      return fail(Error::COMMAND_PARSER_REQUIRED);
    size_t begin = at;
    auto end = commands.scan(commands.context, input, begin, legacy);
    if (!end) {
      result = end;
      return false;
    }
    if (end.position <= begin || end.position > input.size() ||
        input[end.position - 1] != uint32_t(legacy ? '`' : ')'))
      return fail(Error::SYNTAX);
    at = end.position;
    return emit(legacy ? Kind::BACKQUOTED_COMMAND : Kind::COMMAND, begin,
                at - 1, quoted, output);
  }
  bool parameter(bool quoted, bool output, unsigned depth) {
    advance();
    size_t begin = at;
    bool length = peek() == '#';
    if (length)
      advance();
    if (!name_start(peek()))
      return fail(Error::SYNTAX);
    do {
      advance();
    } while (name_part(peek()));
    if (peek() != '}') {
      if (length)
        return fail(Error::SYNTAX);
      bool colon = peek() == ':';
      if (colon)
        advance();
      bool pattern = peek() == '#' || peek() == '%';
      if (pattern) {
        if (colon)
          return fail(Error::SYNTAX);
        uint32_t op = input[at];
        advance();
        if (peek() == op)
          advance();
      } else if (peek() == '-' || peek() == '+' || peek() == '=' ||
                 peek() == '?') {
        advance();
      } else {
        return fail(Error::SYNTAX);
      }
      // Outer double quotes do not quote the pattern-removal operand.
      if (!sequence(Mode::PARAMETER, quoted && !pattern, false, depth + 1))
        return false;
    }
    if (peek() != '}')
      return fail(Error::SYNTAX);
    size_t end = at;
    advance();
    return emit(Kind::BRACED_PARAMETER, begin, end, quoted, output);
  }
  bool dollar(bool quoted, bool output, unsigned depth) {
    size_t begin = at;
    advance();
    if (peek() == '{')
      return parameter(quoted, output, depth);
    if (peek() == '(') {
      advance();
      if (peek() != '(')
        return command(false, quoted, output);
      advance();
      size_t start = at;
      if (!sequence(Mode::ARITHMETIC, true, false, depth + 1))
        return false;
      if (peek() != ')' || peek(1) != ')')
        return fail(Error::SYNTAX);
      size_t end = at;
      advance();
      advance();
      return emit(Kind::ARITHMETIC, start, end, quoted, output);
    }
    if (!quoted && peek() == '\'')
      return single_quote(true, output);
    if (name_start(peek())) {
      size_t start = at;
      advance();
      while (name_part(peek()))
        advance();
      return emit(Kind::PARAMETER, start, at, quoted, output);
    }
    // Special/positional parameters have unspecified wordexp results. Keep
    // their spelling literal rather than consulting libc's process arguments.
    return emit(Kind::LITERAL, begin, begin + 1, quoted, output);
  }
  bool sequence(Mode mode, bool quoted, bool output, unsigned depth) {
    if (depth > 128)
      return fail(Error::NESTING_LIMIT);
    bool word = false;
    size_t parentheses = 0;
    while (at < input.size()) {
      at = skip_continuations(at);
      if (at == input.size())
        break;
      uint32_t c = peek();
      if (!c)
        return fail(Error::SYNTAX);
      if ((mode == Mode::PARAMETER && c == '}') ||
          (mode == Mode::DOUBLE_QUOTE && c == '"') ||
          (mode == Mode::ARITHMETIC && c == ')' && !parentheses))
        return true;
      bool arithmetic = mode == Mode::ARITHMETIC ||
                        mode == Mode::ARITHMETIC_FRAGMENT;
      if (arithmetic) {
        if (c == '(')
          ++parentheses;
        if (c == ')') {
          if (!parentheses)
            return fail(Error::SYNTAX);
          --parentheses;
        }
      }
      if (mode == Mode::TOP && (c == ' ' || c == '\t')) {
        if (word && !emit(Kind::END_WORD, at, at, false, output))
          return false;
        word = false;
        ++at;
        continue;
      }
      if (c == '\\') {
        size_t start = at++;
        if (at == input.size())
          return fail(Error::SYNTAX);
        uint32_t next = peek();
        bool escape = !quoted || next == '$' || next == '`' || next == '\\' ||
                      next == '\n' ||
                      (next == '"' && !arithmetic) ||
                      (next == '}' && mode == Mode::PARAMETER);
        if (escape && next == '\n') {
          ++at;
          continue;
        }
        if (escape) {
          start = at++;
          if (!emit(Kind::LITERAL, start, at, true, output))
            return false;
        } else if (!emit(Kind::LITERAL, start, at, true, output)) {
          return false;
        }
      } else if (c == '\'' && !quoted) {
        if (!single_quote(false, output))
          return false;
      } else if (c == '"' && !arithmetic) {
        ++at;
        // Preserve a quoted empty expansion, not only literal empty quotes.
        if (!emit(Kind::LITERAL, at, at, true, output) ||
            !sequence(Mode::DOUBLE_QUOTE, true, output, depth + 1) ||
            peek() != '"')
          return fail(Error::SYNTAX);
        ++at;
      } else if (c == '$') {
        if (!dollar(quoted, output, depth))
          return false;
      } else if (c == '`') {
        ++at;
        if (!command(true, quoted, output))
          return false;
      } else if ((mode == Mode::TOP || mode == Mode::FRAGMENT) && !quoted &&
                 !word && c == '~') {
        size_t literal = at;
        advance();
        size_t start = at;
        while (name_part(peek()) || peek() == '-' || peek() == '.')
          advance();
        // A quote or substitution within the tilde prefix prevents expansion.
        if (at == input.size() || peek() == '/' || peek() == ' ' ||
            peek() == '\t') {
          if (!emit(Kind::TILDE, start, at, true, output))
            return false;
        } else {
          at = start;
          if (!emit(Kind::LITERAL, literal, literal + 1, false, output))
            return false;
        }
      } else {
        if (mode == Mode::TOP &&
            (c == '\n' || c == '|' || c == '&' || c == ';' || c == '<' ||
             c == '>' || c == '(' || c == ')' || c == '{' || c == '}'))
          return fail(Error::BAD_CHARACTER);
        size_t start = at++;
        if (!emit(Kind::LITERAL, start, at, quoted, output))
          return false;
      }
      word = true;
    }
    if (mode == Mode::FRAGMENT || mode == Mode::ARITHMETIC_FRAGMENT)
      return parentheses == 0 || fail(Error::SYNTAX);
    if (mode != Mode::TOP)
      return fail(Error::SYNTAX);
    return !word || emit(Kind::END_WORD, at, at, false, output);
  }

public:
  Parser(cpp::span<const uint32_t> input, bool no_command, WordSyntaxSink sink,
         WordCommandSyntax commands)
      : input(input), no_command(no_command), sink(sink), commands(commands) {}
  WordSyntaxResult run() {
    sequence(Mode::TOP, false, true, 0);
    return result;
  }
  WordSyntaxResult fragment(bool quoted, bool arithmetic) {
    sequence(arithmetic ? Mode::ARITHMETIC_FRAGMENT : Mode::FRAGMENT,
             quoted || arithmetic, true, 0);
    return result;
  }
};
} // namespace

WordSyntaxResult parse_word_syntax(cpp::span<const uint32_t> input,
                                   bool no_command, WordSyntaxSink sink,
                                   WordCommandSyntax commands) {
  for (size_t i = 0; i != input.size(); ++i)
    if (!input[i])
      return {Error::SYNTAX, i};
  auto checked = Parser(input, no_command, {}, commands).run();
  if (!checked || !sink.emit)
    return checked;
  return Parser(input, no_command, sink, commands).run();
}

WordSyntaxResult parse_word_fragment(cpp::span<const uint32_t> input,
                                    bool quoted, bool arithmetic,
                                    bool no_command, WordSyntaxSink sink,
                                    WordCommandSyntax commands) {
  for (size_t i = 0; i != input.size(); ++i)
    if (!input[i])
      return {Error::SYNTAX, i};
  auto checked = Parser(input, no_command, {}, commands).fragment(quoted, arithmetic);
  if (!checked || !sink.emit)
    return checked;
  return Parser(input, no_command, sink, commands).fragment(quoted, arithmetic);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
