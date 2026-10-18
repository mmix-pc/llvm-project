//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wcsftime.h"
#include "src/__support/common.h"
#include "src/__support/macros/null_check.h"
#include "src/__support/printf_core/writer.h"
#include "src/__support/wchar/character_converter.h"
#include "src/time/strftime_core/strftime_main.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
struct WideTimeBuffer {
  wchar_t *buffer;
  size_t capacity;
  size_t used = 0;
  internal::mbstate state{};

  int append(wchar_t c) {
    if (used == capacity)
      return -1;
    buffer[used++] = c;
    return 0;
  }

  static int write(cpp::string_view bytes, void *context) {
    auto &out = *static_cast<WideTimeBuffer *>(context);
    internal::LocaleCharacterConverter converter(&out.state);
    for (char byte : bytes) {
      int error = converter.push(static_cast<char8_t>(byte));
      if (error)
        return -error;
      if (converter.isFull()) {
        auto c = converter.pop<char32_t>();
        if (!c.has_value())
          return -c.error();
        if (out.append(static_cast<wchar_t>(c.value())) < 0)
          return -1;
      }
    }
    return 0;
  }
};
} // namespace

LLVM_LIBC_FUNCTION(size_t, wcsftime,
                   (wchar_t *__restrict buffer, size_t size,
                    const wchar_t *__restrict format,
                    const struct tm *timeptr)) {
  if (size > 0)
    LIBC_CRASH_ON_NULLPTR(buffer);
  LIBC_CRASH_ON_NULLPTR(format);
  LIBC_CRASH_ON_NULLPTR(timeptr);
  if (size == 0)
    return 0;
  WideTimeBuffer output{buffer, size - 1};
  while (*format) {
    if (*format != L'%') {
      if (output.append(*format++) < 0)
        return 0;
      continue;
    }
    // C17 directives are ASCII; literals never pass through a narrow buffer.
    char directive[4] = {'%', 0, 0, 0};
    size_t length = 1;
    ++format;
    if (*format == L'E' || *format == L'O')
      directive[length++] = static_cast<char>(*format++);
    if (*format && static_cast<unsigned long>(*format) <= 0x7f)
      directive[length++] = static_cast<char>(*format++);

    printf_core::FlushingBuffer sink(nullptr, 0, WideTimeBuffer::write,
                                     &output);
    printf_core::Writer writer(sink);
    auto result = strftime_core::strftime_main(&writer, directive, timeptr);
    if (!result.has_value() || output.state.bytes_stored != 0)
      return 0;
  }
  buffer[output.used] = L'\0';
  return output.used;
}
} // namespace LIBC_NAMESPACE_DECL
