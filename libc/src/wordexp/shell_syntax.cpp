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
#include "src/wordexp/quote.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
using Scalars = cpp::span<const uint32_t>;
using Error = WordSyntaxError;

bool source_position(const WordSource *source, Scalars input, size_t &offset) {
  if (!source)
    return false;
  uintptr_t first = reinterpret_cast<uintptr_t>(source->scalars().data());
  uintptr_t current = reinterpret_cast<uintptr_t>(input.data());
  size_t bytes = source->scalars().size() * sizeof(uint32_t);
  if (current < first || current - first > bytes ||
      (current - first) % sizeof(uint32_t))
    return false;
  offset = (current - first) / sizeof(uint32_t);
  return input.size() <= source->scalars().size() - offset;
}

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
                         unsigned depth, bool quoted = false,
                         const RestartableConverter *converter = nullptr,
                         const WordSource *source = nullptr);

class Parser {
  Scalars input;
  size_t at;
  unsigned depth;
  const RestartableConverter *converter;
  const WordSource *source = nullptr;
  size_t source_offset = 0;
  Token token;
  WordSyntaxResult result;
  struct Here {
    Here *next = nullptr;
    uint32_t *delimiter = nullptr;
    CharVector bytes;
    bool byte_match = false;
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
        if (!checked(command(input, at + 1, true, nesting + 1, true, converter,
                             source)))
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
        if (!checked(command(input, at + 1, true, nesting + 1, false, converter,
                             source)))
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
          return checked(command(input, body, false, nesting + 1, false,
                                 converter, source));
        advance();
        return true;
      }
      return checked(
          command(input, body, false, nesting + 1, false, converter, source));
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
        if (!checked(command(input, at + 1, true, depth + 1, false, converter,
                             source)))
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
          while (peek() == '\t' &&
                 (!here->byte_match || source_bytes(at).size() == 1))
            ++at;
        size_t count = 0;
        bool same = true;
        auto compare = [&](cpp::string_view bytes) {
          for (char byte : bytes) {
            same &= count < here->bytes.length() &&
                    byte == here->bytes.c_str()[count];
            ++count;
          }
        };
        while (peek() && peek() != '\n') {
          if (!here->quoted && peek() == '\\' && at + 1 < input.size() &&
              input[at + 1] == '\n') {
            if (here->byte_match) {
              compare(source_bytes(at, true));
              compare(source_bytes(at + 1, true));
            }
            at += 2;
            continue;
          }
          if (here->byte_match) {
            compare(source_bytes(at));
          } else {
            same &= count < here->length && input[at] == here->delimiter[count];
            ++count;
          }
          ++at;
        }
        if (peek() == '\n') {
          if (here->byte_match)
            compare(source_bytes(at, true));
          ++at;
        }
        if (same &&
            count == (here->byte_match ? here->bytes.length() : here->length)) {
          if (!here->quoted) {
            auto parsed = Parser(input.subspan(body, line - body), 0, depth + 1,
                                 converter, source)
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
        if (!checked(command(input, at + 1, true, depth + 1, false, converter,
                             source)))
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
  cpp::string_view source_bytes(size_t index, bool remove_ascii = false) const {
    const auto &unit = source->units()[source_offset + index];
    return source->bytes().substr(unit.begin,
                                  unit.end - unit.begin - size_t(remove_ascii));
  }
  bool byte_here_word(Here &here) {
    WordQuoteState state;
    // Earlier designations establish state, but are not delimiter bytes.
    state.state = source->units()[source_offset + token.begin].before;
    auto append = [&](cpp::string_view bytes) {
      for (char byte : bytes)
        if (!here.bytes.append(byte))
          return fail(Error::RESOURCE, ENOMEM);
      while (state.valid && !bytes.empty()) {
        uint32_t scalar;
        auto length = converter->decode(
            &scalar,
            {reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()},
            state.state);
        if (!length || !*length) {
          state.valid = false;
          break;
        }
        if (*length == size_t(-2))
          break;
        bytes.remove_prefix(*length);
      }
      return true;
    };
    uint32_t quote = 0;
    for (size_t i = token.begin; i < token.end; ++i) {
      uint32_t c = input[i];
      if (!quote && c == '$' && i + 1 < token.end && input[i + 1] == '\'') {
        if (!append(source_bytes(i, true)) ||
            !append(source_bytes(i + 1, true)))
          return false;
        size_t start = i + 2, end = start;
        while (end < token.end && input[end] != '\'') {
          if (input[end] == '\\')
            ++end;
          ++end;
        }
        if (end >= token.end)
          return fail();
        CharVector bytes;
        auto rendered = render_word_quote(*source, source_offset + start,
                                          source_offset + end, *converter,
                                          bytes, &state, false);
        if (!rendered)
          return fail(rendered.sink_error == ENOMEM ? Error::RESOURCE
                                                    : rendered.error,
                      rendered.sink_error);
        for (size_t j = 0; j < bytes.length(); ++j)
          if (!here.bytes.append(bytes.c_str()[j]))
            return fail(Error::RESOURCE, ENOMEM);
        if (!append(source_bytes(end, true)))
          return false;
        here.quoted = true;
        i = end;
        continue;
      }
      if (c == quote || (!quote && (c == '\'' || c == '"'))) {
        quote = c == quote ? 0 : c;
        here.quoted = true;
        if (!append(source_bytes(i, true)))
          return false;
        continue;
      }
      if (c == '\\' && quote != '\'' && i + 1 < token.end) {
        uint32_t next = input[i + 1];
        if (!quote || next == '$' || next == '`' || next == '\\' ||
            next == '"' || next == '\n') {
          if (!append(source_bytes(i, true)))
            return false;
          ++i;
          here.quoted |= next != '\n';
          if (!append(source_bytes(i, next == '\n')))
            return false;
          continue;
        }
      }
      if (!append(source_bytes(i)))
        return false;
    }
    // The following ASCII token can carry the final shift reset. It is part
    // of the delimiter's byte spelling, not the separating ASCII character.
    if (token.end < input.size() && input[token.end] < 128 &&
        !append(source_bytes(token.end, true)))
      return false;
    here.byte_match = true;
    return !quote || fail();
  }
  bool here_word(bool strip) {
    if (converter && converter->state_dependent() && !source)
      return fail(Error::COMMAND_PARSER_REQUIRED);
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
    if (converter && converter->state_dependent())
      return byte_here_word(*here);
    here->delimiter = new (ac) uint32_t[token.end - token.begin];
    if (!ac)
      return fail(Error::RESOURCE, ENOMEM);
    uint32_t quote = 0;
    for (size_t i = token.begin; i < token.end; ++i) {
      uint32_t c = input[i];
      if (!quote && c == '$' && i + 1 < token.end && input[i + 1] == '\'') {
        // Stateless valid encodings have a unique spelling per character.
        // Stateful delimiters must instead compare their original byte spans.
        if (!converter)
          return fail(Error::COMMAND_PARSER_REQUIRED);
        size_t start = i + 2, end = start;
        while (end < token.end && input[end] != '\'') {
          if (input[end] == '\\')
            ++end;
          ++end;
        }
        if (end >= token.end)
          return fail();
        CharVector bytes;
        auto rendered = render_word_quote(input.subspan(start, end - start),
                                          *converter, bytes);
        if (!rendered) {
          if (rendered.sink_error == ENOMEM)
            rendered.error = Error::RESOURCE;
          rendered.position += start;
          result = rendered;
          return false;
        }
        WordSource decoded;
        if (int error =
                decoded.assign({bytes.c_str(), bytes.length()}, *converter))
          return fail(error == ENOMEM ? Error::RESOURCE : Error::SINK_ERROR,
                      error);
        for (uint32_t scalar : decoded.scalars())
          here->delimiter[here->length++] = scalar;
        here->quoted = true;
        i = end;
        continue;
      }
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
  Parser(Scalars input, size_t begin, unsigned depth,
         const RestartableConverter *converter = nullptr,
         const WordSource *owned = nullptr)
      : input(input), at(begin), depth(depth), converter(converter) {
    // Never apply an owned byte map to an unrelated scalar allocation.
    if (source_position(owned, input, source_offset))
      source = owned;
  }
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
                         unsigned depth, bool quoted,
                         const RestartableConverter *converter,
                         const WordSource *source) {
  if (depth > 128)
    return {Error::NESTING_LIMIT, begin};
  if (!legacy)
    return Parser(input, begin, depth, converter, source).run(true);
  size_t end = begin;
  while (end < input.size() && input[end] != '`') {
    if (input[end] == '\\' && end + 1 < input.size() &&
        backquote_escape(input[end + 1], quoted))
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
        backquote_escape(input[i + 1], quoted)) {
      if (input[++i] == '\n')
        continue;
    }
    positions[count] = i;
    text[count++] = input[i];
  }
  positions[count] = end;
  WordSyntaxResult parsed;
  size_t offset = 0;
  if (converter && converter->state_dependent() &&
      source_position(source, input, offset)) {
    CharVector bytes;
    WordSource prepared;
    int error = source->command(offset + begin, offset + end, true, quoted,
                                *converter, bytes);
    if (!error)
      error = prepared.assign({bytes.c_str(), bytes.length()}, *converter);
    if (!error) {
      // The byte-preserving preparation and scalar escape removal must agree
      // before positions from one representation can describe the other.
      if (prepared.scalars().size() != count)
        error = EILSEQ;
      for (size_t i = 0; !error && i < count; ++i)
        if (prepared.scalars()[i] != text[i])
          error = EILSEQ;
    }
    parsed = error ? WordSyntaxResult{error == ENOMEM ? Error::RESOURCE
                                                      : Error::SINK_ERROR,
                                      0, error}
                   : Parser(prepared.scalars(), 0, depth, converter, &prepared)
                         .run(false);
  } else {
    parsed = Parser({text, count}, 0, depth, converter).run(false);
  }
  parsed.position = parsed ? end + 1 : positions[parsed.position];
  ::free(positions);
  ::free(text);
  return parsed;
}
} // namespace

WordSyntaxResult scan_word_command(Scalars input, size_t begin, bool backquoted,
                                   bool quoted) {
  if (begin > input.size())
    return {Error::SYNTAX, input.size()};
  for (auto scalar : input)
    if (!scalar)
      return {Error::SYNTAX, begin};
  return command(input, begin, backquoted, 0, quoted);
}
WordCommandSyntax word_command_syntax() {
  return {nullptr,
          [](void *, Scalars input, size_t begin, bool legacy, bool quoted) {
            return scan_word_command(input, begin, legacy, quoted);
          }};
}

WordCommandSyntax WordCommandParser::service() {
  return {this,
          [](void *p, Scalars input, size_t begin, bool legacy, bool quoted) {
            if (begin > input.size())
              return WordSyntaxResult{Error::SYNTAX, input.size()};
            for (auto scalar : input)
              if (!scalar)
                return WordSyntaxResult{Error::SYNTAX, begin};
            auto &self = *static_cast<WordCommandParser *>(p);
            return command(input, begin, legacy, 0, quoted, &self.converter,
                           self.source);
          },
          [](void *p, const WordSource *source) {
            auto &self = *static_cast<WordCommandParser *>(p);
            const WordSource *old = self.source;
            self.source = source;
            return old;
          }};
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
