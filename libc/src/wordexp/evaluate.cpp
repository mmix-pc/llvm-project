//===-- Word expansion result composition ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/evaluate.h"
#include "hdr/errno_macros.h"
#include "hdr/wordexp_macros.h"
#include "src/wordexp/ifs.h"
#include "src/wordexp/paths.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
WordEvaluationResult system_error(int error) {
  bool resource =
      error == ENOMEM || error == EAGAIN || error == EMFILE || error == ENFILE;
  return {resource ? WRDE_NOSPACE : WRDE_SYNTAX, error};
}

WordEvaluationResult expansion_error(WordExpansionResult result) {
  switch (result.error) {
  case WordExpansionError::NONE:
    return {};
  case WordExpansionError::UNDEFINED:
    return {WRDE_BADVAL};
  case WordExpansionError::PARAMETER_ERROR:
    return {WRDE_SYNTAX};
  case WordExpansionError::SERVICE_REQUIRED:
    return system_error(ENOTSUP);
  case WordExpansionError::SYSTEM:
    return system_error(result.system_error);
  case WordExpansionError::SYNTAX:
    switch (result.syntax.error) {
    case WordSyntaxError::BAD_CHARACTER:
      return {WRDE_BADCHAR};
    case WordSyntaxError::COMMAND_FORBIDDEN:
      return {WRDE_CMDSUB};
    case WordSyntaxError::COMMAND_PARSER_REQUIRED:
      return system_error(ENOTSUP);
    case WordSyntaxError::NESTING_LIMIT:
      return system_error(ELOOP);
    case WordSyntaxError::RESOURCE:
    case WordSyntaxError::SINK_ERROR:
      return system_error(result.syntax.sink_error);
    default:
      return {WRDE_SYNTAX};
    }
  }
  __builtin_unreachable();
}

struct Fields {
  WordResults &results;
  RestartableConverter converter;
  const __locale_data *ctype;
  const WordExpansionServices &services;

  static int append(void *context, cpp::span<const ExpansionPart> parts) {
    auto &self = *static_cast<Fields *>(context);
    if (!self.services.lookup)
      return ENOTSUP;
    auto ifs = self.services.lookup(self.services.context, "IFS");
    if (!ifs)
      return ifs.error();
    WordDelimiters delimiters;
    if (int error =
            delimiters.assign(ifs->set, ifs->value, self.converter, self.ctype))
      return error;
    return append_word_paths(parts, delimiters.delimiters(), self.converter,
                             self.results);
  }
};
} // namespace

WordEvaluationResult evaluate_wordexp(cpp::string_view input, wordexp_t &result,
                                      int flags, RestartableConverter converter,
                                      const __locale_data *ctype,
                                      const CollationData *collation,
                                      WordExpansionServices services) {
  // Snapshot before REUSE can free an earlier result borrowed by input.
  WordSource source;
  int source_error = source.assign(input, converter);
  WordResultTransaction transaction(result);
  if (int status = transaction.begin(flags))
    return {status, status == WRDE_NOSPACE ? ENOMEM : EINVAL};
  WordEvaluationResult outcome;
  if (source_error) {
    outcome = system_error(source_error);
  } else {
    Fields fields{transaction.results(), converter, ctype, services};
    outcome = expansion_error(expand_word_fragments(
        source, flags & WRDE_NOCMD, flags & WRDE_UNDEF, converter, ctype,
        collation, services, {&fields, Fields::append}));
  }
  transaction.finish(outcome.status);
  return outcome;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
