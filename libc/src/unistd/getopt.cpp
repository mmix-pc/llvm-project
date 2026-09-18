//===-- Implementation of getopt ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/getopt.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/common.h"
#include "src/stdio/fprintf.h"
#include "src/stdio/stderr.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_VARIABLE(char *, optarg) = nullptr;
LLVM_LIBC_VARIABLE(int, optind) = 1;
LLVM_LIBC_VARIABLE(int, optopt) = 0;
LLVM_LIBC_VARIABLE(int, opterr) = 1;

namespace {
enum class Ordering { Stop, Permute, ReturnOperands };
struct GetoptContext {
  char **argument;
  int *index;
  int *option;
  unsigned *position;
  int *errors;
  FILE *stream;
  int first_operand = 1;
  int last_operand = 1;
  int previous_index = 1;
  bool initialized = false;
  bool finished = false;
  Ordering ordering = Ordering::Stop;

  template <typename... Ts>
  void report(bool silent, const char *fmt, Ts... ts) {
    if (!silent && *errors)
      LIBC_NAMESPACE::fprintf(stream ? stream : LIBC_NAMESPACE::stderr, fmt,
                              ts...);
  }
};
unsigned optpos;
GetoptContext context{&optarg, &optind, &optopt, &optpos, &opterr, nullptr};

void reverse(char **argv, int begin, int end) {
  while (begin < --end) {
    char *tmp = argv[begin];
    argv[begin++] = argv[end];
    argv[end] = tmp;
  }
}

// Rotate adjacent operand/option ranges without allocating or changing strings.
void exchange(char *const argv[], GetoptContext &ctx) {
  char **slots = const_cast<char **>(argv);
  reverse(slots, ctx.first_operand, ctx.last_operand);
  reverse(slots, ctx.last_operand, *ctx.index);
  reverse(slots, ctx.first_operand, *ctx.index);
  ctx.first_operand += *ctx.index - ctx.last_operand;
  ctx.last_operand = *ctx.index;
}

int parse(int argc, char *const argv[], const char *optstring,
          const struct option *options, int *long_index, bool permute,
          GetoptContext &ctx) {
  int &index = *ctx.index;
  unsigned &position = *ctx.position;
  *ctx.argument = nullptr;
  const char prefix = *optstring;
  if (prefix == '+' || prefix == '-')
    ++optstring;
  bool silent = *optstring == ':';
  if (silent)
    ++optstring;

  if (!ctx.initialized || index <= 0 || index != ctx.previous_index) {
    if (index <= 0)
      index = 1;
    position = 0;
    ctx.first_operand = ctx.last_operand = index;
    ctx.finished = false;
    ctx.initialized = true;
    ctx.ordering = prefix == '-'               ? Ordering::ReturnOperands
                   : prefix == '+' || !permute ? Ordering::Stop
                                               : Ordering::Permute;
  }
  if (ctx.finished || !argv)
    return -1;
  auto at_end = [&] { return index >= argc || !argv[index]; };
  auto operand = [&] { return argv[index][0] != '-' || !argv[index][1]; };
  auto finish = [&] {
    if (ctx.ordering == Ordering::Permute &&
        ctx.first_operand != ctx.last_operand)
      index = ctx.first_operand;
    position = 0;
    ctx.finished = true;
    return -1;
  };
  const char *program = argc > 0 && argv[0] ? argv[0] : "";

  if (!position) {
    if (ctx.ordering == Ordering::Permute) {
      if (ctx.first_operand != ctx.last_operand && ctx.last_operand != index)
        exchange(argv, ctx);
      else if (ctx.last_operand != index)
        ctx.first_operand = index;
      while (!at_end() && operand())
        ++index;
      ctx.last_operand = index;
    }
    if (at_end())
      return finish();
    if (cpp::string_view(argv[index]) == "--") {
      ++index;
      if (ctx.ordering == Ordering::Permute) {
        if (ctx.first_operand != ctx.last_operand && ctx.last_operand != index)
          exchange(argv, ctx);
        else if (ctx.first_operand == ctx.last_operand)
          ctx.first_operand = index;
        ctx.last_operand = argc;
        index = argc;
      }
      return finish();
    }
    if (operand()) {
      if (ctx.ordering == Ordering::Stop)
        return finish();
      *ctx.argument = argv[index++];
      return 1;
    }
    if (options && argv[index][1] == '-') {
      const char *text = argv[index++] + 2;
      cpp::string_view full(text);
      size_t length = 0;
      while (length < full.size() && full[length] != '=')
        ++length;
      cpp::string_view name = full.substr(0, length);
      int match = -1;
      bool ambiguous = false;
      for (int i = 0; options[i].name; ++i) {
        cpp::string_view candidate(options[i].name);
        if (name.empty() || !candidate.starts_with(name))
          continue;
        if (candidate == name) {
          match = i;
          ambiguous = false;
          break;
        }
        if (match < 0)
          match = i;
        else if (options[i].has_arg != options[match].has_arg ||
                 options[i].flag != options[match].flag ||
                 options[i].val != options[match].val)
          ambiguous = true;
      }
      if (match < 0 || ambiguous) {
        *ctx.option = 0;
        ctx.report(silent, "%s: %s option -- %s\n", program,
                   ambiguous ? "ambiguous" : "unrecognized", text);
        return '?';
      }
      const auto &selected = options[match];
      bool attached = length != full.size();
      if (attached && selected.has_arg == 0) {
        *ctx.option = selected.val;
        ctx.report(silent, "%s: option does not allow an argument -- %s\n",
                   program, selected.name);
        return '?';
      }
      if (attached)
        *ctx.argument = const_cast<char *>(text + length + 1);
      else if (selected.has_arg == 1) {
        if (at_end()) {
          *ctx.option = selected.val;
          ctx.report(silent, "%s: option requires an argument -- %s\n", program,
                     selected.name);
          return silent ? ':' : '?';
        }
        *ctx.argument = argv[index++];
      }
      if (long_index)
        *long_index = match;
      if (selected.flag) {
        *selected.flag = selected.val;
        return 0;
      }
      return selected.val;
    }
    position = 1;
  }

  unsigned char current = argv[index][position++];
  const char *match = optstring;
  while (*match && static_cast<unsigned char>(*match) != current)
    ++match;
  if (!*match || current == ':') {
    if (!argv[index][position]) {
      ++index;
      position = 0;
    }
    *ctx.option = current;
    ctx.report(silent, "%s: illegal option -- %c\n", program, current);
    return '?';
  }
  if (match[1] == ':') {
    if (argv[index][position])
      *ctx.argument = argv[index] + position;
    else if (match[2] != ':') {
      ++index;
      if (at_end()) {
        position = 0;
        *ctx.option = current;
        ctx.report(silent, "%s: option requires an argument -- %c\n", program,
                   current);
        return silent ? ':' : '?';
      }
      *ctx.argument = argv[index];
    }
    ++index;
    position = 0;
  } else if (!argv[index][position]) {
    ++index;
    position = 0;
  }
  return current;
}
} // namespace

namespace impl {
#ifndef LIBC_COPT_PUBLIC_PACKAGING
void set_getopt_state(char **argument, int *index, int *option,
                      unsigned *position, int *errors, FILE *stream) {
  context = {argument, index, option, position, errors, stream};
}
#endif

int getopt_internal(int argc, char *const argv[], const char *optstring,
                    const struct option *options, int *index, bool permute) {
  int result = parse(argc, argv, optstring, options, index, permute, context);
  context.previous_index = *context.index;
  return result;
}
} // namespace impl

LLVM_LIBC_FUNCTION(int, getopt,
                   (int argc, char *const argv[], const char *optstring)) {
  // Retain POSIX stop-at-operand ordering; getopt_long supplies GNU
  // permutation.
  return impl::getopt_internal(argc, argv, optstring, nullptr, nullptr, false);
}
} // namespace LIBC_NAMESPACE_DECL
