//===-- Message selection and recoding ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/message_translation.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/realloc.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/wchar/encoding_descriptor.h"
#include "src/libintl/plural_forms.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
ErrorOr<cpp::string_view> selected_message(const MoCatalogView &catalog,
                                           cpp::string_view key,
                                           cpp::optional<unsigned long> count) {
  auto message = catalog.find(key);
  if (!message)
    return Error(message.error());
  size_t index = 0;
  if (count &&
      message->original.find_first_of('\0') != cpp::string_view::npos) {
    auto metadata = catalog.plural_forms();
    if (!metadata)
      return Error(metadata.error());
    auto forms = PluralForms::parse(*metadata);
    if (!forms)
      return Error(forms.error());
    auto chosen = forms->select(*count);
    if (!chosen)
      return Error(chosen.error());
    index = *chosen;
    // A valid expression must not hide a truncated or overfull translation.
    size_t present = 1;
    for (char c : message->translation)
      present += c == '\0';
    if (present != forms->size())
      return Error(EINVAL);
  }
  auto selected = message->translation;
  for (size_t i = 0; i != index; ++i) {
    size_t end = selected.find_first_of('\0');
    if (end == cpp::string_view::npos)
      return Error(EINVAL);
    selected.remove_prefix(end + 1);
  }
  return selected.substr(0, selected.find_first_of('\0'));
}

struct Buffer {
  char *data = nullptr;
  size_t size = 0;
  ~Buffer() { ::free(data); }
  bool resize(size_t capacity) {
    auto *replacement = static_cast<char *>(::realloc(data, capacity));
    if (!replacement)
      return false;
    data = replacement;
    size = capacity;
    return true;
  }
  char *release() {
    char *result = data;
    data = nullptr;
    return result;
  }
};
} // namespace

ErrorOr<char *> translate_message(const MoCatalogView &catalog,
                                  cpp::string_view key,
                                  cpp::optional<unsigned long> count,
                                  cpp::string_view output_codeset,
                                  LocaleDataFile *mapping,
                                  const TransliterationData *transliteration,
                                  size_t *output_size) {
  auto message = selected_message(catalog, key, count);
  if (!message)
    return Error(message.error());
  auto charset = catalog.charset();
  if (!charset && charset.error() != ENOENT)
    return Error(charset.error());
  Buffer result;
  if (!charset) {
    // Without a declared encoding, the catalog bytes are returned unchanged.
    if (!result.resize(message->size() + 1))
      return Error(ENOMEM);
    for (size_t i = 0; i != message->size(); ++i)
      result.data[i] = (*message)[i];
    result.data[message->size()] = 0;
    if (output_size)
      *output_size = message->size();
    return result.release();
  }
  auto opened = EncodingDescriptor::create(*charset, output_codeset, mapping);
  if (!opened)
    return Error(opened.error());
  struct Descriptor {
    EncodingDescriptor *value;
    ~Descriptor() { value->destroy(); }
  } descriptor{*opened};

  if (!result.resize(message->size() < 64 ? 64 : message->size() + 1))
    return Error(ENOMEM);
  cpp::span<const uint8_t> input{
      reinterpret_cast<const uint8_t *>(message->data()), message->size()};
  size_t used = 0;
  bool resetting = false;
  for (;;) {
    cpp::span<uint8_t> output{reinterpret_cast<uint8_t *>(result.data + used),
                              result.size - used - 1};
    auto converted =
        resetting ? descriptor.value->reset(&output)
                  : descriptor.value->convert(input, output, transliteration);
    used = result.size - 1 - output.size();
    if (converted) {
      if (resetting) {
        result.data[used] = 0;
        if (output_size)
          *output_size = used;
        return result.release();
      }
      resetting = true;
      continue;
    }
    if (converted.error() != E2BIG)
      return Error(converted.error());
    if (result.size > cpp::numeric_limits<size_t>::max() / 2)
      return Error(EOVERFLOW);
    if (!result.resize(result.size * 2))
      return Error(ENOMEM);
  }
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
