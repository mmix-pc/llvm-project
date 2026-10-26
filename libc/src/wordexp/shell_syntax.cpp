//===-- Shell syntax for word command substitutions -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/shell_syntax.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/alloc-checker.h"
#include "src/wordexp/dollar_quote.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
using Scalars = cpp::span<const uint32_t>;
using Error = WordSyntaxError;

bool first_name(uint32_t c) {
  return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool next_name(uint32_t c) { return first_name(c) || (c >= '0' && c <= '9'); }

enum class Kind {
  END,
  WORD,
  NEWLINE,
  LPAREN,
  RPAREN,
  PIPE,
  AND,
  OR,
  SEMI,
  BACKGROUND,
  CASE_END,
  FALLTHROUGH,
  REDIRECT,
  HERE,
  HERE_TABS,
  IO_NUMBER
};
struct Token {
  Kind kind = Kind::END;
  size_t begin = 0, end = 0;
  bool plain = true, name = false;
};

WordSyntaxResult command(Scalars input, size_t begin, bool legacy,
                         unsigned depth);

class Parser {
  Scalars input;
  size_t at;
  unsigned depth;
  Token token;
  WordSyntaxResult result;
  struct Here {
    Here *next = nullptr;
    uint32_t *delimiter = nullptr;
    size_t length = 0;
    bool strip = false, quoted = false;
    ~Here() { ::free(delimiter); }
  };
  Here *pending = nullptr, *tail = nullptr;

  bool fail(Error error = Error::SYNTAX, int detail = 0) {
    if (result)
      result = {error, at, detail};
    return false;
  }
  uint32_t peek() const { return at < input.size() ? input[at] : 0; }
  void continuations() {
    while (at + 1 < input.size() && input[at] == '\\' && input[at + 1] == '\n')
      at += 2;
  }
  void advance() {
    ++at;
    continuations();
  }
  bool checked(WordSyntaxResult parsed) {
    if (!parsed) {
      result = parsed;
      return false;
    }
    at = parsed.position;
    return true;
  }
  bool single(bool dollar) {
    size_t start = ++at;
    while (peek() && peek() != '\'') {
      if (dollar && peek() == '\\') {
        ++at;
        if (!peek())
          return fail();
      }
      ++at;
    }
    if (!peek())
      return fail();
    if (dollar) {
      auto quote = decode_dollar_quote(input.subspan(start, at - start));
      if (!quote) {
        quote.position += start;
        result = quote;
        return false;
      }
    }
    ++at;
    return true;
  }
  bool double_quote(unsigned nesting) {
    if (nesting > 128)
      return fail(Error::NESTING_LIMIT);
    ++at;
    while (peek() && peek() != '"') {
      if (peek() == '\\') {
        ++at;
        if (!peek())
          return fail();
        ++at;
      } else if (peek() == '$') {
        if (!dollar(nesting + 1, true))
          return false;
      } else if (peek() == '`') {
        if (!checked(command(input, at + 1, true, nesting + 1)))
          return false;
      } else {
        ++at;
      }
    }
    if (!peek())
      return fail();
    ++at;
    return true;
  }
  bool balanced(uint32_t close, unsigned nesting, bool arithmetic) {
    if (nesting > 128)
      return fail(Error::NESTING_LIMIT);
    while (peek()) {
      if (peek() == close) {
        advance();
        return true;
      }
      if (peek() == '\\') {
        ++at;
        if (!peek())
          return fail();
        ++at;
      } else if (peek() == '\'') {
        if (!single(false))
          return false;
      } else if (peek() == '"') {
        if (!double_quote(nesting + 1))
          return false;
      } else if (peek() == '$') {
        if (!dollar(nesting + 1, false))
          return false;
      } else if (peek() == '`') {
        if (!checked(command(input, at + 1, true, nesting + 1)))
          return false;
      } else if (arithmetic && peek() == '(') {
        advance();
        if (!balanced(')', nesting + 1, true))
          return false;
      } else {
        advance();
      }
    }
    return fail();
  }
  bool dollar(unsigned nesting, bool quoted) {
    advance();
    if (peek() == '(') {
      advance();
      size_t body = at;
      if (peek() == '(') {
        advance();
        if (!balanced(')', nesting + 1, true))
          return false;
        if (peek() != ')')
          return checked(command(input, body, false, nesting + 1));
        advance();
        return true;
      }
      return checked(command(input, body, false, nesting + 1));
    }
    if (peek() == '{') {
      advance();
      return balanced('}', nesting + 1, false);
    }
    return quoted || peek() != '\'' || single(true);
  }
  bool word() {
    token = {Kind::WORD, at, at};
    bool name = true, number = true;
    size_t characters = 0;
    while (peek()) {
      uint32_t c = peek();
      if (c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' ||
          c == '|' || c == '(' || c == ')' || c == '<' || c == '>')
        break;
      name &= characters ? next_name(c) : first_name(c);
      number &= c >= '0' && c <= '9';
      ++characters;
      if (c == '\\') {
        token.plain = false;
        ++at;
        if (!peek())
          return fail();
        ++at;
      } else if (c == '\'') {
        token.plain = false;
        if (!single(false))
          return false;
      } else if (c == '"') {
        token.plain = false;
        if (!double_quote(depth + 1))
          return false;
      } else if (c == '$') {
        token.plain = false;
        if (!dollar(depth + 1, false))
          return false;
      } else if (c == '`') {
        token.plain = false;
        if (!checked(command(input, at + 1, true, depth + 1)))
          return false;
      } else {
        advance();
      }
      continuations();
    }
    token.end = at;
    token.name = name && token.plain;
    if (number && token.plain && (peek() == '<' || peek() == '>'))
      token.kind = Kind::IO_NUMBER;
    return characters || fail();
  }
  bool equal(const char *name) const {
    if (token.kind != Kind::WORD || !token.plain)
      return false;
    size_t i = token.begin;
    for (; i < token.end; ++i) {
      if (input[i] == '\\' && i + 1 < token.end && input[i + 1] == '\n') {
        ++i;
        continue;
      }
      if (!*name || input[i] != uint32_t(*name++))
        return false;
    }
    return !*name;
  }
  bool read_documents() {
    while (pending) {
      auto *here = pending;
      bool found = false;
      size_t body = at;
      while (at < input.size()) {
        size_t line = at;
        if (here->strip)
          while (peek() == '\t')
            ++at;
        size_t count = 0;
        bool same = true;
        while (peek() && peek() != '\n') {
          if (!here->quoted && peek() == '\\' && at + 1 < input.size() &&
              input[at + 1] == '\n') {
            at += 2;
            continue;
          }
          same &= count < here->length && input[at] == here->delimiter[count];
          ++count;
          ++at;
        }
        if (peek() == '\n')
          ++at;
        if (same && count == here->length) {
          if (!here->quoted) {
            auto parsed = Parser(input.subspan(body, line - body), 0, depth + 1)
                              .document_expansions();
            if (!parsed) {
              parsed.position += body;
              result = parsed;
              return false;
            }
          }
          found = true;
          break;
        }
      }
      if (!found)
        return fail();
      pending = here->next;
      here->~Here();
      ::free(here);
    }
    tail = nullptr;
    return true;
  }
  WordSyntaxResult document_expansions() {
    if (depth > 128)
      return {Error::NESTING_LIMIT, at};
    // Quotes in the body are data, not shell quoting. Only an unquoted
    // delimiter permits substitutions, with the here-document escape rules.
    while (peek()) {
      if (peek() == '\\' && at + 1 < input.size() &&
          (input[at + 1] == '$' || input[at + 1] == '`' ||
           input[at + 1] == '\\' || input[at + 1] == '\n')) {
        at += 2;
      } else if (peek() == '$') {
        if (!dollar(depth + 1, true))
          return result;
      } else if (peek() == '`') {
        if (!checked(command(input, at + 1, true, depth + 1)))
          return result;
      } else {
        ++at;
      }
    }
    return result;
  }
  bool next() {
    if (!result)
      return false;
    continuations();
    while (peek() == ' ' || peek() == '\t')
      advance();
    if (peek() == '#')
      while (peek() && peek() != '\n')
        ++at;
    token = {Kind::END, at, at};
    uint32_t c = peek();
    if (!c)
      return true;
    switch (c) {
    case '\n':
      ++at;
      token.kind = Kind::NEWLINE;
      if (!read_documents())
        return false;
      break;
    case '(':
      advance();
      token.kind = Kind::LPAREN;
      break;
    case ')':
      advance();
      token.kind = Kind::RPAREN;
      break;
    case ';':
      advance();
      token.kind = Kind::SEMI;
      if (peek() == ';') {
        advance();
        token.kind = Kind::CASE_END;
      } else if (peek() == '&') {
        advance();
        token.kind = Kind::FALLTHROUGH;
      }
      break;
    case '&':
      advance();
      token.kind = Kind::BACKGROUND;
      if (peek() == '&') {
        advance();
        token.kind = Kind::AND;
      }
      break;
    case '|':
      advance();
      token.kind = Kind::PIPE;
      if (peek() == '|') {
        advance();
        token.kind = Kind::OR;
      }
      break;
    case '<':
    case '>':
      advance();
      token.kind = Kind::REDIRECT;
      if (peek() == c) {
        advance();
        if (c == '<') {
          token.kind = Kind::HERE;
          if (peek() == '-') {
            advance();
            token.kind = Kind::HERE_TABS;
          }
        }
      } else if (peek() == '&' || (c == '<' && peek() == '>') ||
                 (c == '>' && peek() == '|')) {
        advance();
      }
      break;
    default:
      return word();
    }
    token.end = at;
    return true;
  }
  bool linebreak() {
    while (token.kind == Kind::NEWLINE)
      if (!next())
        return false;
    return true;
  }
  bool here_word(bool strip) {
    AllocChecker ac;
    Here *here = new (ac) Here;
    if (!ac)
      return fail(Error::RESOURCE, ENOMEM);
    if (tail)
      tail->next = here;
    else
      pending = here;
    tail = here;
    here->strip = strip;
    here->delimiter = new (ac) uint32_t[token.end - token.begin];
    if (!ac)
      return fail(Error::RESOURCE, ENOMEM);
    uint32_t quote = 0;
    for (size_t i = token.begin; i < token.end; ++i) {
      uint32_t c = input[i];
      if (!quote && c == '$' && i + 1 < token.end && input[i + 1] == '\'')
        // Numeric byte escapes need the original-byte encoding adapter. Never
        // silently reinterpret them as decoded Unicode delimiter characters.
        return fail(Error::COMMAND_PARSER_REQUIRED);
      if (c == quote) {
        quote = 0;
        continue;
      }
      if (!quote && (c == '\'' || c == '"')) {
        quote = c;
        here->quoted = true;
        continue;
      }
      if (c == '\\' && quote != '\'' && i + 1 < token.end) {
        uint32_t n = input[i + 1];
        if (!quote || n == '$' || n == '`' || n == '\\' || n == '"' ||
            n == '\n') {
          ++i;
          if (n == '\n')
            continue;
          c = n;
          here->quoted = true;
        }
      }
      here->delimiter[here->length++] = c;
    }
    return !quote || fail();
  }
  bool redirect() {
    if (token.kind == Kind::IO_NUMBER && !next())
      return false;
    Kind kind = token.kind;
    if (kind != Kind::REDIRECT && kind != Kind::HERE && kind != Kind::HERE_TABS)
      return fail();
    if (!next() || token.kind != Kind::WORD)
      return fail();
    if ((kind == Kind::HERE || kind == Kind::HERE_TABS) &&
        !here_word(kind == Kind::HERE_TABS))
      return false;
    return next();
  }
  bool is_redirect() const {
    return token.kind == Kind::IO_NUMBER || token.kind == Kind::REDIRECT ||
           token.kind == Kind::HERE || token.kind == Kind::HERE_TABS;
  }
  bool stopped() const {
    return token.kind == Kind::END || token.kind == Kind::RPAREN ||
           token.kind == Kind::CASE_END || token.kind == Kind::FALLTHROUGH ||
           equal("}") || equal("then") || equal("else") || equal("elif") ||
           equal("fi") || equal("do") || equal("done") || equal("esac");
  }
  bool list(bool empty) {
    if (!linebreak())
      return false;
    if (stopped())
      return empty || fail();
    for (;;) {
      if (!pipeline())
        return false;
      while (token.kind == Kind::AND || token.kind == Kind::OR)
        if (!next() || !linebreak() || !pipeline())
          return false;
      if (token.kind != Kind::NEWLINE && token.kind != Kind::SEMI &&
          token.kind != Kind::BACKGROUND)
        return true;
      if (!next() || !linebreak())
        return false;
      if (stopped())
        return true;
    }
  }
  bool take(const char *name) {
    if (!equal(name))
      return fail();
    return next();
  }
  bool compound() {
    if (++depth > 128)
      return fail(Error::NESTING_LIMIT);
    bool ok = compound_body();
    --depth;
    if (!ok)
      return false;
    while (is_redirect())
      if (!redirect())
        return false;
    return true;
  }
  bool compound_body() {
    if (token.kind == Kind::LPAREN) {
      if (!next() || !list(false) || token.kind != Kind::RPAREN)
        return fail();
      return next();
    }
    if (equal("{")) {
      return next() && list(false) && take("}");
    }
    if (equal("if")) {
      do {
        if (!next() || !list(false) || !take("then") || !list(false))
          return false;
      } while (equal("elif"));
      if (equal("else") && (!next() || !list(false)))
        return false;
      return take("fi");
    }
    if (equal("while") || equal("until"))
      return next() && list(false) && take("do") && list(false) && take("done");
    if (equal("for")) {
      if (!next() || !token.name)
        return fail();
      if (!next() || !linebreak())
        return false;
      if (equal("in")) {
        if (!next())
          return false;
        while (token.kind == Kind::WORD)
          if (!next())
            return false;
        if (token.kind != Kind::SEMI && token.kind != Kind::NEWLINE)
          return fail();
        if (!next() || !linebreak())
          return false;
      } else if (token.kind == Kind::SEMI) {
        if (!next() || !linebreak())
          return false;
      }
      return take("do") && list(false) && take("done");
    }
    if (equal("case")) {
      if (!next() || token.kind != Kind::WORD)
        return fail();
      if (!next() || !linebreak() || !take("in") || !linebreak())
        return false;
      while (!equal("esac")) {
        if (token.kind == Kind::LPAREN && !next())
          return false;
        if (token.kind != Kind::WORD)
          return fail();
        if (!next())
          return false;
        while (token.kind == Kind::PIPE) {
          if (!next() || token.kind != Kind::WORD)
            return fail();
          if (!next())
            return false;
        }
        if (token.kind != Kind::RPAREN)
          return fail();
        if (!next() || !list(true))
          return false;
        if (equal("esac"))
          break;
        if (token.kind != Kind::CASE_END && token.kind != Kind::FALLTHROUGH)
          return fail();
        if (!next() || !linebreak())
          return false;
      }
      return take("esac");
    }
    return fail();
  }
  bool is_compound() const {
    return token.kind == Kind::LPAREN || equal("{") || equal("if") ||
           equal("for") || equal("while") || equal("until") || equal("case");
  }
  bool simple() {
    if (is_compound())
      return compound();
    if (stopped() || equal("in") || equal("!"))
      return fail();
    bool consumed = false;
    while (token.kind == Kind::WORD || is_redirect()) {
      if (is_redirect()) {
        if (!redirect())
          return false;
      } else {
        bool fname = !consumed && token.name;
        if (!next())
          return false;
        if (fname && token.kind == Kind::LPAREN) {
          if (!next() || token.kind != Kind::RPAREN)
            return fail();
          if (!next() || !linebreak() || !is_compound())
            return fail();
          return compound();
        }
      }
      consumed = true;
    }
    return consumed || fail();
  }
  bool pipeline() {
    if (equal("!") && !next())
      return false;
    if (!simple())
      return false;
    while (token.kind == Kind::PIPE)
      if (!next() || !linebreak() || !simple())
        return false;
    return true;
  }

public:
  Parser(Scalars input, size_t begin, unsigned depth)
      : input(input), at(begin), depth(depth) {}
  ~Parser() {
    while (pending) {
      Here *next = pending->next;
      pending->~Here();
      ::free(pending);
      pending = next;
    }
  }
  WordSyntaxResult run(bool closing) {
    if (depth > 128)
      return {Error::NESTING_LIMIT, at};
    if (!next() || !list(true))
      return result;
    if (pending || token.kind != (closing ? Kind::RPAREN : Kind::END))
      fail();
    if (result)
      result.position = closing ? token.begin + 1 : at;
    return result;
  }
};

WordSyntaxResult command(Scalars input, size_t begin, bool legacy,
                         unsigned depth) {
  if (depth > 128)
    return {Error::NESTING_LIMIT, begin};
  if (!legacy)
    return Parser(input, begin, depth).run(true);
  size_t end = begin;
  while (end < input.size() && input[end] != '`') {
    if (input[end] == '\\' && end + 1 < input.size() &&
        (input[end + 1] == '\\' || input[end + 1] == '$' ||
         input[end + 1] == '`'))
      ++end;
    ++end;
  }
  if (end == input.size())
    return {Error::SYNTAX, end};
  AllocChecker ac;
  uint32_t *text = new (ac) uint32_t[end - begin + 1];
  if (!ac)
    return {Error::RESOURCE, begin, ENOMEM};
  size_t *positions = new (ac) size_t[end - begin + 1];
  if (!ac) {
    ::free(text);
    return {Error::RESOURCE, begin, ENOMEM};
  }
  size_t count = 0;
  for (size_t i = begin; i < end; ++i) {
    if (input[i] == '\\' && i + 1 < end &&
        (input[i + 1] == '\\' || input[i + 1] == '$' || input[i + 1] == '`'))
      ++i;
    positions[count] = i;
    text[count++] = input[i];
  }
  positions[count] = end;
  auto parsed = Parser({text, count}, 0, depth).run(false);
  parsed.position = parsed ? end + 1 : positions[parsed.position];
  ::free(positions);
  ::free(text);
  return parsed;
}
} // namespace

WordSyntaxResult scan_word_command(Scalars input, size_t begin,
                                   bool backquoted) {
  if (begin > input.size())
    return {Error::SYNTAX, input.size()};
  for (auto scalar : input)
    if (!scalar)
      return {Error::SYNTAX, begin};
  return command(input, begin, backquoted, 0);
}
WordCommandSyntax word_command_syntax() {
  return {nullptr, [](void *, Scalars input, size_t begin, bool legacy) {
            return scan_word_command(input, begin, legacy);
          }};
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
