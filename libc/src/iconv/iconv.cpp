//===-- Implementation of iconv -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/iconv/iconv.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/iconv/context.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(size_t, iconv,
                   (iconv_t descriptor, char **__restrict input,
                    size_t *__restrict input_left, char **__restrict output,
                    size_t *__restrict output_left)) {
  auto fail = [](int error) -> size_t {
    libc_errno = error;
    return size_t(-1);
  };
  if (!descriptor || descriptor == reinterpret_cast<iconv_t>(-1))
    return fail(EBADF);
  auto *context = reinterpret_cast<internal::IconvContext *>(descriptor);
  bool reset = !input || !*input;
  if (reset && (!output || !*output)) {
    auto result = context->reset(nullptr);
    return result ? *result : fail(result.error());
  }
  if (!output || !*output || !output_left || (!reset && !input_left))
    return fail(EINVAL);
  cpp::span<uint8_t> out(reinterpret_cast<uint8_t *>(*output), *output_left);
  ErrorOr<size_t> result = size_t(0);
  if (reset) {
    result = context->reset(&out);
  } else {
    cpp::span<const uint8_t> in(reinterpret_cast<const uint8_t *>(*input),
                                *input_left);
    result = context->convert(in, out);
    *input = reinterpret_cast<char *>(const_cast<uint8_t *>(in.data()));
    *input_left = in.size();
  }
  *output = reinterpret_cast<char *>(out.data());
  *output_left = out.size();
  return result ? *result : fail(result.error());
}
} // namespace LIBC_NAMESPACE_DECL
