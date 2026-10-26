//===-- Linux word expansion services -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/runtime.h"
#include "hdr/errno_macros.h"
#include "hdr/wordexp_macros.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/fwrite.h"
#include "src/stdio/stderr.h"
#include "src/stdlib/environ_internal.h"
#include "src/wordexp/command.h"
#include "src/wordexp/environment.h"
#include "src/wordexp/shell_syntax.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
struct Runtime {
  WordEnvironment environment;
  WordExpansionServices base = environment.services();
  WordCommandParser parser;
  bool show_error;

  Runtime(bool show_error, RestartableConverter converter)
      : parser(converter), show_error(show_error) {}
  static ErrorOr<WordVariable> lookup(void *p, cpp::string_view name) {
    auto &self = *static_cast<Runtime *>(p);
    return self.base.lookup(self.base.context, name);
  }
  static int assign(void *p, cpp::string_view name, cpp::string_view value) {
    auto &self = *static_cast<Runtime *>(p);
    return self.base.assign(self.base.context, name, value);
  }
  static ErrorOr<WordVariable> home(void *p, cpp::string_view user) {
    auto &self = *static_cast<Runtime *>(p);
    return self.base.home(self.base.context, user);
  }
  static int command(void *p, const WordSource &source, WordToken token,
                     RestartableConverter converter, CharVector &output) {
    auto &self = *static_cast<Runtime *>(p);
    char *empty[] = {nullptr};
    char **variables = EnvironmentManager::get_instance().begin();
    auto status = capture_word_command(
        source, token, converter, variables ? variables : empty,
        self.show_error, {&output, [](void *p, cpp::string_view bytes) {
                            auto &output = *static_cast<CharVector *>(p);
                            for (char c : bytes)
                              if (!output.append(c))
                                return ENOMEM;
                            return 0;
                          }});
    // A completed command's nonzero exit status does not discard its output.
    return status ? 0 : status.error();
  }
  static int diagnostic(void *, cpp::string_view name, cpp::string_view text) {
    const cpp::string_view parts[] = {name, ": ", text, "\n"};
    for (auto part : parts) {
      int saved = libc_errno;
      libc_errno = 0;
      auto written = LIBC_NAMESPACE::fwrite(part.data(), 1, part.size(),
                                            LIBC_NAMESPACE::stderr);
      int error = libc_errno;
      libc_errno = saved;
      if (written != part.size())
        return error ? error : EIO;
    }
    return 0;
  }
  WordExpansionServices services() {
    WordExpansionServices result = base;
    result.context = this;
    result.lookup = lookup;
    result.assign = assign;
    result.home = home;
    result.source_command = command;
    result.diagnostic = diagnostic;
    result.commands = parser.service();
    return result;
  }
};
} // namespace

WordEvaluationResult evaluate_wordexp_runtime(cpp::string_view input,
                                              wordexp_t &result, int flags,
                                              RestartableConverter converter,
                                              const __locale_data *ctype,
                                              const CollationData *collation) {
  Runtime runtime(flags & WRDE_SHOWERR, converter);
  return evaluate_wordexp(input, result, flags, converter, ctype, collation,
                          runtime.services());
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
