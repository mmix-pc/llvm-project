//===-- Message plural expression parser and evaluator --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/plural_forms.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

class PluralParser {
  using Op = PluralForms::Op;
  using Node = PluralForms::Node;
  cpp::string_view input;
  size_t offset = 0;
  int error = 0;
  PluralForms &forms;

  void space() {
    while (offset < input.size() &&
           (input[offset] == ' ' || input[offset] == '\t' ||
            input[offset] == '\n' || input[offset] == '\r'))
      ++offset;
  }
  bool take(cpp::string_view token) {
    space();
    if (input.substr(offset, token.size()) != token)
      return false;
    offset += token.size();
    return true;
  }
  unsigned long number() {
    space();
    size_t start = offset;
    unsigned long value = 0;
    constexpr auto max = cpp::numeric_limits<unsigned long>::max();
    while (offset < input.size() && input[offset] >= '0' &&
           input[offset] <= '9') {
      unsigned digit = input[offset++] - '0';
      if (value > (max - digit) / 10) {
        error = EOVERFLOW;
        return 0;
      }
      value = value * 10 + digit;
    }
    if (offset == start)
      error = EINVAL;
    return value;
  }
  unsigned add(Op op, unsigned left = 0, unsigned right = 0, unsigned third = 0,
               unsigned long value = 0) {
    if (error)
      return 0;
    unsigned depth = 1;
    auto include = [&](unsigned child) {
      unsigned child_depth = forms.nodes[child].depth + 1;
      if (child_depth > depth)
        depth = child_depth;
    };
    if (op != Op::NUMBER && op != Op::N)
      include(left);
    if (op != Op::NUMBER && op != Op::N && op != Op::NOT)
      include(right);
    if (op == Op::CONDITIONAL)
      include(third);
    if (forms.used == PluralForms::MAX_NODES ||
        depth > PluralForms::MAX_DEPTH) {
      error = EOVERFLOW;
      return 0;
    }
    unsigned index = forms.used++;
    forms.nodes[index] = Node{
        op,   uint8_t(depth), uint16_t(left), uint16_t(right), uint16_t(third),
        value};
    return index;
  }
  unsigned expression(unsigned minimum, unsigned depth) {
    if (error)
      return 0;
    if (depth > PluralForms::MAX_DEPTH) {
      error = EOVERFLOW;
      return 0;
    }
    unsigned left;
    if (take("!")) {
      left = add(Op::NOT, expression(7, depth + 1));
    } else if (take("(")) {
      left = expression(0, depth + 1);
      if (error)
        return 0;
      if (!take(")"))
        error = EINVAL;
    } else if (take("n")) {
      left = add(Op::N);
    } else {
      unsigned long value = number();
      left = add(Op::NUMBER, 0, 0, 0, value);
    }
    struct Operator {
      const char *text;
      Op op;
      unsigned precedence;
    };
    static constexpr Operator operators[] = {
        {"||", Op::OR, 1}, {"&&", Op::AND, 2}, {"==", Op::EQ, 3},
        {"!=", Op::NE, 3}, {"<=", Op::LE, 4},  {">=", Op::GE, 4},
        {"<", Op::LT, 4},  {">", Op::GT, 4},   {"+", Op::ADD, 5},
        {"-", Op::SUB, 5}, {"*", Op::MUL, 6},  {"/", Op::DIV, 6},
        {"%", Op::MOD, 6}};
    while (!error) {
      bool found = false;
      for (const auto &op : operators) {
        if (op.precedence >= minimum && take(op.text)) {
          unsigned right = expression(op.precedence + 1, depth + 1);
          left = add(op.op, left, right);
          found = true;
          break;
        }
      }
      if (found)
        continue;
      if (minimum == 0 && take("?")) {
        unsigned yes = expression(0, depth + 1);
        if (error)
          return 0;
        if (!take(":")) {
          error = EINVAL;
          return 0;
        }
        unsigned no = expression(0, depth + 1);
        left = add(Op::CONDITIONAL, left, yes, no);
      }
      break;
    }
    return left;
  }

public:
  PluralParser(cpp::string_view input, PluralForms &forms)
      : input(input), forms(forms) {}
  int parse() {
    if (input.size() > PluralForms::MAX_LENGTH)
      return EOVERFLOW;
    bool have_count = false, have_expression = false;
    for (;;) {
      if (take("nplurals")) {
        if (have_count || !take("="))
          return EINVAL;
        forms.count = number();
        have_count = true;
        if (!forms.count)
          return error ? error : EINVAL;
      } else if (take("plural")) {
        if (have_expression || !take("="))
          return EINVAL;
        forms.root = expression(0, 1);
        have_expression = true;
      } else {
        return EINVAL;
      }
      if (error)
        return error;
      bool separator = take(";");
      space();
      if (offset == input.size())
        return have_count && have_expression ? 0 : EINVAL;
      if (!separator)
        return EINVAL;
    }
  }
};

ErrorOr<PluralForms> PluralForms::parse(cpp::string_view metadata) {
  PluralForms forms;
  if (int error = PluralParser(metadata, forms).parse())
    return Error(error);
  return forms;
}

ErrorOr<unsigned long> PluralForms::evaluate(unsigned index,
                                             unsigned long n) const {
  const Node &node = nodes[index];
  if (node.op == Op::NUMBER)
    return node.value;
  if (node.op == Op::N)
    return n;
  auto left = evaluate(node.left, n);
  if (!left)
    return Error(left.error());
  if (node.op == Op::NOT)
    return static_cast<unsigned long>(!*left);
  if (node.op == Op::CONDITIONAL)
    return evaluate(*left ? node.right : node.third, n);
  if ((node.op == Op::AND && !*left) || (node.op == Op::OR && *left))
    return static_cast<unsigned long>(node.op == Op::OR);
  auto right = evaluate(node.right, n);
  if (!right)
    return Error(right.error());
  switch (node.op) {
  case Op::ADD:
    return *left + *right;
  case Op::SUB:
    return *left - *right;
  case Op::MUL:
    return *left * *right;
  case Op::DIV:
  case Op::MOD:
    if (!*right)
      return Error(EDOM);
    return node.op == Op::DIV ? *left / *right : *left % *right;
  case Op::LT:
    return static_cast<unsigned long>(*left < *right);
  case Op::LE:
    return static_cast<unsigned long>(*left <= *right);
  case Op::GT:
    return static_cast<unsigned long>(*left > *right);
  case Op::GE:
    return static_cast<unsigned long>(*left >= *right);
  case Op::EQ:
    return static_cast<unsigned long>(*left == *right);
  case Op::NE:
    return static_cast<unsigned long>(*left != *right);
  case Op::AND:
  case Op::OR:
    return static_cast<unsigned long>(*right != 0);
  default:
    return Error(EINVAL);
  }
}

ErrorOr<unsigned long> PluralForms::select(unsigned long n) const {
  if (!count || !used)
    return Error(EINVAL);
  auto index = evaluate(root, n);
  if (!index)
    return Error(index.error());
  if (*index >= count)
    return Error(EDOM);
  return *index;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
