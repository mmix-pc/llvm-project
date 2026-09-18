//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/stdio/vdprintf.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/arg_list.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/printf_core/error_mapper.h"
#include "src/__support/printf_core/printf_main.h"
#include "src/__support/printf_core/writer.h"
#include "src/unistd/write.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
int write_fd(cpp::string_view text, void *target) {
  int fd = *static_cast<int *>(target);
  while (!text.empty()) {
    auto written = LIBC_NAMESPACE::write(fd, text.data(), text.size());
    if (written < 0)
      return -int(libc_errno);
    if (written == 0)
      return -EIO;
    text = text.substr(static_cast<size_t>(written));
  }
  return printf_core::WRITE_OK;
}
} // namespace
LLVM_LIBC_FUNCTION(int, vdprintf,
                   (int fd, const char *__restrict format, va_list vlist)) {
  internal::ArgList args(vlist);
  char buffer[1024];
  printf_core::FlushingBuffer output(buffer, sizeof(buffer), write_fd, &fd);
  printf_core::Writer writer(output);
  auto result = printf_core::printf_main(&writer, format, args);
  if (!result) {
    libc_errno = printf_core::internal_error_to_errno(result.error());
    return -1;
  }
  int error = output.flush_to_stream();
  if (error != printf_core::WRITE_OK) {
    libc_errno = -error;
    return -1;
  }
  if (result.value() > static_cast<size_t>(cpp::numeric_limits<int>::max())) {
    libc_errno = EOVERFLOW;
    return -1;
  }
  return static_cast<int>(result.value());
}
} // namespace LIBC_NAMESPACE_DECL
