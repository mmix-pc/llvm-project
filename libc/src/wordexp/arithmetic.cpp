//===-- Signed integer arithmetic for word expansion ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/arithmetic.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {

constexpr long MIN = cpp::numeric_limits<long>::min();
constexpr long MAX = cpp::numeric_limits<long>::max();

enum class Op {
  END,
  ASSIGN,
  ADD_ASSIGN,
  SUB_ASSIGN,
  MUL_ASSIGN,
  DIV_ASSIGN,
  MOD_ASSIGN,
  SHL_ASSIGN,
  SHR_ASSIGN,
  AND_ASSIGN,
  XOR_ASSIGN,
  OR_ASSIGN,
  CONDITIONAL,
  OR,
  AND,
  BIT_OR,
  XOR,
  BIT_AND,
  EQ,
  NE,
  LT,
  LE,
  GT,
  GE,
  SHL,
  SHR,
  ADD,
  SUB,
  MUL,
  DIV,
  MOD
};

struct Operator {
  cpp::string_view text;
  Op op;
  unsigned precedence;
};

// Longest tokens precede their prefixes, regardless of precedence.
constexpr Operator OPERATORS[] = {
    {"<<=", Op::SHL_ASSIGN, 1}, {">>=", Op::SHR_ASSIGN, 1},
    {"+=", Op::ADD_ASSIGN, 1},  {"-=", Op::SUB_ASSIGN, 1},
    {"*=", Op::MUL_ASSIGN, 1},  {"/=", Op::DIV_ASSIGN, 1},
    {"%=", Op::MOD_ASSIGN, 1},  {"&=", Op::AND_ASSIGN, 1},
    {"^=", Op::XOR_ASSIGN, 1},  {"|=", Op::OR_ASSIGN, 1},
    {"||", Op::OR, 3},          {"&&", Op::AND, 4},
    {"==", Op::EQ, 8},          {"!=", Op::NE, 8},
    {"<=", Op::LE, 9},          {">=", Op::GE, 9},
    {"<<", Op::SHL, 10},        {">>", Op::SHR, 10},
    {"=", Op::ASSIGN, 1},       {"?", Op::CONDITIONAL, 2},
    {"|", Op::BIT_OR, 5},       {"^", Op::XOR, 6},
    {"&", Op::BIT_AND, 7},      {"<", Op::LT, 9},
    {">", Op::GT, 9},           {"+", Op::ADD, 11},
    {"-", Op::SUB, 11},         {"*", Op::MUL, 12},
    {"/", Op::DIV, 12},         {"%", Op::MOD, 12}};

struct Value {
  long number = 0;
  cpp::string_view name{};
  bool loaded = true;
  // Permit the magnitude of LONG_MIN immediately after unary minus without
  // representing that positive value in a signed long.
  bool minimum_magnitude = false;
};

class Parser {
  cpp::string_view input;
  ArithmeticVariables variables;
  size_t at = 0;
  int error = 0;
  static constexpr unsigned MAX_DEPTH = 128;

  static bool first(char c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  }
  static bool digit(char c) { return c >= '0' && c <= '9'; }
  void space() {
    while (at < input.size() &&
           (input[at] == ' ' || input[at] == '\t' || input[at] == '\n' ||
            input[at] == '\r' || input[at] == '\v' || input[at] == '\f'))
      ++at;
  }
  bool take(char c) {
    space();
    if (at == input.size() || input[at] != c)
      return false;
    ++at;
    return true;
  }
  long load(Value &value, bool active) {
    if (!active || error)
      return 0;
    if (value.minimum_magnitude) {
      error = EOVERFLOW;
      return 0;
    }
    if (!value.loaded) {
      if (!variables.read) {
        error = EINVAL;
        return 0;
      }
      auto read = variables.read(variables.context, value.name);
      if (!read) {
        error = read.error();
        return 0;
      }
      value.number = *read;
      value.loaded = true;
    }
    return value.number;
  }
  Value number(bool active) {
    unsigned base = 10;
    if (input[at] == '0') {
      base = 8;
      if (at + 1 < input.size() &&
          (input[at + 1] == 'x' || input[at + 1] == 'X')) {
        base = 16;
        at += 2;
      }
    }
    size_t start = at;
    unsigned long value = 0;
    constexpr unsigned long limit = static_cast<unsigned long>(MAX) + 1;
    bool overflow = false;
    while (at < input.size()) {
      char c = input[at];
      unsigned d = digit(c)               ? c - '0'
                   : c >= 'a' && c <= 'f' ? c - 'a' + 10
                   : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                          : 16;
      if (d >= base)
        break;
      ++at;
      if (value > (limit - d) / base)
        overflow = true;
      else if (!overflow)
        value = value * base + d;
    }
    if (start == at)
      error = EINVAL;
    if (active && overflow)
      error = EOVERFLOW;
    if (value == limit)
      return {MIN, {}, true, true};
    return {static_cast<long>(value)};
  }
  long apply(Op op, long left, long right) {
    long result = 0;
    switch (op) {
    case Op::ADD:
    case Op::ADD_ASSIGN:
      if (__builtin_add_overflow(left, right, &result))
        error = EOVERFLOW;
      return result;
    case Op::SUB:
    case Op::SUB_ASSIGN:
      if (__builtin_sub_overflow(left, right, &result))
        error = EOVERFLOW;
      return result;
    case Op::MUL:
    case Op::MUL_ASSIGN:
      if (__builtin_mul_overflow(left, right, &result))
        error = EOVERFLOW;
      return result;
    case Op::DIV:
    case Op::MOD:
    case Op::DIV_ASSIGN:
    case Op::MOD_ASSIGN:
      if (!right) {
        error = EDOM;
        return 0;
      }
      if (left == MIN && right == -1) {
        error = EOVERFLOW;
        return 0;
      }
      return op == Op::DIV || op == Op::DIV_ASSIGN ? left / right
                                                   : left % right;
    case Op::SHL:
    case Op::SHL_ASSIGN:
    case Op::SHR:
    case Op::SHR_ASSIGN:
      if (right < 0 || right >= long(sizeof(long) * 8)) {
        error = EDOM;
        return 0;
      }
      if (op == Op::SHR || op == Op::SHR_ASSIGN)
        return left >> right;
      if (left < 0 || left > (MAX >> right)) {
        error = EOVERFLOW;
        return 0;
      }
      return static_cast<unsigned long>(left) << right;
    case Op::BIT_AND:
    case Op::AND_ASSIGN:
      return left & right;
    case Op::BIT_OR:
    case Op::OR_ASSIGN:
      return left | right;
    case Op::XOR:
    case Op::XOR_ASSIGN:
      return left ^ right;
    case Op::EQ:
      return left == right;
    case Op::NE:
      return left != right;
    case Op::LT:
      return left < right;
    case Op::LE:
      return left <= right;
    case Op::GT:
      return left > right;
    case Op::GE:
      return left >= right;
    case Op::AND:
      return left && right;
    case Op::OR:
      return left || right;
    case Op::ASSIGN:
      return right;
    default:
      error = EINVAL;
      return 0;
    }
  }
  Value expression(unsigned minimum, unsigned depth, bool active) {
    if (error)
      return {};
    if (depth > MAX_DEPTH) {
      error = EOVERFLOW;
      return {};
    }
    space();
    if (at == input.size()) {
      error = EINVAL;
      return {};
    }
    Value left;
    char c = input[at];
    if ((c == '+' || c == '-') && at + 1 < input.size() && input[at + 1] == c) {
      error = EINVAL;
      return {};
    }
    if (c == '+' || c == '-' || c == '!' || c == '~') {
      ++at;
      auto operand = expression(13, depth + 1, active);
      if (c == '-' && operand.minimum_magnitude) {
        left = {MIN};
      } else {
        long value = load(operand, active);
        if (active && !error) {
          if (c == '-' && value == MIN)
            error = EOVERFLOW;
          else
            left.number = c == '+'   ? value
                          : c == '-' ? -value
                          : c == '!' ? !value
                                     : ~value;
        }
      }
    } else if (c == '(') {
      ++at;
      left = expression(1, depth + 1, active);
      if (error)
        return {};
      if (!take(')'))
        error = EINVAL;
    } else if (first(c)) {
      size_t start = at++;
      while (at < input.size() && (first(input[at]) || digit(input[at])))
        ++at;
      left = {0, input.substr(start, at - start), false};
    } else if (digit(c)) {
      left = number(active);
    } else {
      error = EINVAL;
      return {};
    }

    while (!error) {
      space();
      if (at + 1 < input.size() && (input[at] == '+' || input[at] == '-') &&
          input[at + 1] == input[at]) {
        error = EINVAL;
        return {};
      }
      Operator op{{}, Op::END, 0};
      for (const auto &candidate : OPERATORS) {
        if (input.substr(at, candidate.text.size()) == candidate.text) {
          op = candidate;
          break;
        }
      }
      if (op.precedence < minimum)
        break;
      at += op.text.size();
      bool assignment = op.precedence == 1;
      if (assignment && left.name.empty()) {
        error = EINVAL;
        return {};
      }
      long a = op.op == Op::ASSIGN ? 0 : load(left, active);
      if (op.op == Op::CONDITIONAL) {
        auto yes = expression(1, depth + 1, active && a);
        long y = load(yes, active && a);
        if (error)
          return {};
        if (!take(':')) {
          error = EINVAL;
          return {};
        }
        auto no = expression(2, depth + 1, active && !a);
        long n = load(no, active && !a);
        left = {a ? y : n};
        continue;
      }
      bool selected =
          active && !(op.op == Op::AND && !a) && !(op.op == Op::OR && a);
      auto right = expression(op.precedence + !assignment, depth + 1, selected);
      long b = load(right, selected);
      long result = 0;
      if (active && !error) {
        result = apply(op.op, a, b);
        if (assignment && !error) {
          if (!variables.write)
            error = EINVAL;
          else
            error = variables.write(variables.context, left.name, result);
        }
      }
      // C assignment and conditional expressions are not lvalues.
      left = {result};
    }
    return left;
  }

public:
  Parser(cpp::string_view input, ArithmeticVariables variables)
      : input(input), variables(variables) {}
  ErrorOr<long> run(bool active) {
    space();
    if (at == input.size())
      return 0;
    auto result = expression(1, 0, active);
    long value = load(result, active);
    space();
    if (!error && at != input.size())
      error = EINVAL;
    if (error)
      return Error(error);
    return value;
  }
};
} // namespace

ErrorOr<long> evaluate_wordexp_arithmetic(cpp::string_view input,
                                          ArithmeticVariables variables) {
  auto syntax = Parser(input, {}).run(false);
  if (!syntax)
    return Error(syntax.error());
  return Parser(input, variables).run(true);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
