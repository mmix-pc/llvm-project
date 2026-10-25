//===-- Owned encoding conversion state -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/wchar/encoding_descriptor.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/alloc-checker.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<EncodingDescriptor *>
EncodingDescriptor::create(cpp::string_view from, cpp::string_view to,
                           LocaleDataFile *mapping_file) {
  cpp::optional<ChineseEncodingData> tables;
  auto initial = EncodingConverter::from_names(from, to);
  if (!initial) {
    if (!mapping_file)
      return Error(initial.error());
    auto data = ChineseEncodingData::read(mapping_file->view());
    if (!data)
      return Error(data.error());
    tables = cpp::optional<ChineseEncodingData>(*data);
    initial = EncodingConverter::from_names(from, to, &*tables);
    if (!initial)
      return Error(initial.error());
  }

  AllocChecker ac;
  auto *descriptor = new (ac) EncodingDescriptor;
  if (!ac)
    return Error(ENOMEM);
  if (tables) {
    if (!mapping_file->retain()) {
      descriptor->destroy();
      return Error(EOVERFLOW);
    }
    descriptor->file = mapping_file;
    descriptor->mapping = tables;
  }
  // Rebind the converter to the descriptor's stable member, not the temporary
  // table view used for validation above. Publication occurs only afterwards.
  auto converter = EncodingConverter::from_names(
      from, to, tables ? &*descriptor->mapping : nullptr);
  if (!converter) {
    descriptor->destroy();
    return Error(converter.error());
  }
  descriptor->converter = cpp::optional<EncodingConverter>(*converter);
  return descriptor;
}

EncodingDescriptor::~EncodingDescriptor() {
  if (file)
    file->release();
}

void EncodingDescriptor::destroy() {
  this->~EncodingDescriptor();
  ::free(this);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
