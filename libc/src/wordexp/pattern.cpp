//===-- Parameter pattern removal ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/pattern.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/alloc-checker.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
ErrorOr<size_t> decode(cpp::string_view value, RestartableConverter converter,
                       uint32_t *output) {
  mbstate state{};
  size_t count = 0;
  while (!value.empty()) {
    uint32_t scalar;
    auto read = converter.decode(
        &scalar,
        {reinterpret_cast<const uint8_t *>(value.data()), value.size()}, state);
    if (!read)
      return Error(read.error());
    if (*read == size_t(-2)) {
      if (!converter.boundary(state))
        return Error(EILSEQ);
      break;
    }
    if (!*read || *read > value.size() || !scalar)
      return Error(EILSEQ);
    if (output)
      output[count] = scalar;
    ++count;
    value.remove_prefix(*read);
  }
  return count;
}

int encode(cpp::span<const uint32_t> value, RestartableConverter converter,
           CharVector &output) {
  mbstate state{};
  auto append = [&](uint32_t scalar) -> int {
    uint8_t bytes[MAX_LOCALE_CHARACTER_BYTES];
    auto written = converter.encode(scalar, bytes, state);
    if (!written)
      return written.error();
    size_t count = *written - (scalar == 0);
    for (size_t i = 0; i != count; ++i)
      if (!output.append(char(bytes[i])))
        return ENOMEM;
    return 0;
  };
  for (uint32_t scalar : value)
    if (int error = append(scalar))
      return error;
  return append(0);
}

struct Scalars {
  uint32_t *data = nullptr;
  ~Scalars() { ::free(data); }
};
} // namespace

ErrorOr<size_t> word_parameter_length(cpp::string_view value,
                                      RestartableConverter converter) {
  return decode(value, converter, nullptr);
}

ErrorOr<size_t>
remove_word_pattern(cpp::string_view pattern, cpp::string_view value,
                    ParameterExpansion::Operation operation,
                    RestartableConverter converter, const __locale_data *ctype,
                    const CollationData *collation, CharVector &output) {
  using Op = ParameterExpansion::Operation;
  bool suffix = operation == Op::SUFFIX || operation == Op::LONGEST_SUFFIX;
  bool longest =
      operation == Op::LONGEST_PREFIX || operation == Op::LONGEST_SUFFIX;
  if (!suffix && operation != Op::PREFIX && operation != Op::LONGEST_PREFIX)
    return Error(EINVAL);
  auto length = decode(value, converter, nullptr);
  if (!length)
    return Error(length.error());
  if (*length > cpp::numeric_limits<size_t>::max() / sizeof(uint32_t))
    return Error(EOVERFLOW);
  Scalars storage;
  if (*length) {
    AllocChecker ac;
    storage.data = new (ac) uint32_t[*length];
    if (!ac)
      return Error(ENOMEM);
    auto filled = decode(value, converter, storage.data);
    if (!filled)
      return Error(filled.error());
  }
  cpp::span<const uint32_t> scalars(storage.data, *length);
  size_t removed = 0;
  // Include the empty candidate. Try longest-first only for doubled operators;
  // fnmatch's pathname/leading-period restrictions do not apply to parameters.
  for (size_t step = 0;; ++step) {
    size_t count = longest ? *length - step : step;
    size_t begin = suffix ? *length - count : 0;
    CharVector candidate;
    if (int error = encode(scalars.subspan(begin, count), converter, candidate))
      return Error(error);
    auto match =
        match_path_pattern(pattern, {candidate.c_str(), candidate.length()}, 0,
                           converter, ctype, collation);
    if (!match)
      return Error(match.error());
    if (*match) {
      removed = count;
      break;
    }
    if (step == *length)
      break;
  }
  if (!removed) {
    // Preserve the exact bytes when no characters are removed, including
    // redundant shift designations and a shift-only tail.
    for (char c : value)
      if (!output.append(c))
        return Error(ENOMEM);
  } else if (int error = encode(
                 scalars.subspan(suffix ? 0 : removed, *length - removed),
                 converter, output)) {
    return Error(error);
  }
  return removed;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
