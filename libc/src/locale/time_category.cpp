//===-- Time category construction ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/time_category.h"
#include "hdr/errno_macros.h"
#include "src/__support/alloc-checker.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
ErrorOr<char *> join(const TimeLocaleData &data, TimeLocaleData::Field field) {
  size_t size = 1;
  size_t count = data.count(field);
  for (size_t i = 0; i != count; ++i) {
    auto text = data.text(field, i).value().bytes;
    // A field may reference shared file payload repeatedly. Bound the expanded
    // query string independently rather than assuming the file bounds its sum.
    size_t extra = text.size() - 1 + (i != 0);
    if (extra > LocaleDataView::MAX_SIZE - size)
      return Error(EOVERFLOW);
    size += extra;
  }
  AllocChecker ac;
  char *result = new (ac) char[size];
  if (!ac)
    return Error(ENOMEM);
  size_t offset = 0;
  for (size_t i = 0; i != count; ++i) {
    if (i)
      result[offset++] = ';';
    auto text = data.text(field, i).value().bytes;
    for (size_t j = 0; j + 1 != text.size(); ++j)
      result[offset++] = char(text[j]);
  }
  result[offset] = '\0';
  return result;
}
} // namespace

ErrorOr<__locale_data *> create_time_category(LocaleDataFile &file) {
  auto data = TimeLocaleData::read(file.view());
  if (!data)
    return Error(data.error());
  auto category = create_locale_category(file, true);
  if (!category)
    return Error(category.error());
  auto eras = join(*data, TimeLocaleData::ERAS);
  if (!eras) {
    release_locale_category(*category);
    return Error(eras.error());
  }
  (*category)->eras = *eras;
  auto digits = join(*data, TimeLocaleData::ALTERNATIVE_DIGITS);
  if (!digits) {
    release_locale_category(*category);
    return Error(digits.error());
  }
  (*category)->alternative_digits = *digits;
  (*category)->time = *data;
  return category;
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
