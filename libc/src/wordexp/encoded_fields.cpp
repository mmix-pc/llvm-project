//===-- State-dependent word field splitting ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/encoded_fields.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
bool same(const mbstate &a, const mbstate &b) {
  return a.partial == b.partial && a.bytes_stored == b.bytes_stored &&
         a.total_bytes == b.total_bytes;
}

struct Encoder {
  FieldSink sink;
  RestartableConverter converter;
  mbstate required{}, after{}, output{};
  bool known = true;
  bool split_boundary = false;
  uint32_t character = 0;

  static int append(void *context, cpp::string_view bytes, bool quoted) {
    auto &self = *static_cast<Encoder *>(context);
    self.split_boundary = false;
    bool transition = !same(self.required, self.output);
    if (transition && self.known && self.character) {
      // Dropping a delimiter can discard a designation irrelevant to a later
      // ASCII character. Do not add those bytes to an otherwise plain name.
      auto probe = self.output;
      uint32_t scalar;
      auto length = self.converter.decode(
          &scalar,
          {reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()},
          probe);
      if (length && *length == bytes.size() && scalar == self.character &&
          (scalar < 128 || same(probe, self.after)))
        transition = false;
    }
    if (!bytes.empty() && self.known && transition) {
      uint8_t prefix[MAX_LOCALE_STATE_PREFIX_BYTES];
      auto length =
          self.converter.transition(self.required, self.output, prefix);
      if (!length)
        return length.error();
      if (*length)
        if (int error = self.sink.append(
                self.sink.context,
                {reinterpret_cast<const char *>(prefix), *length}, quoted))
          return error;
    }
    if (int error = self.sink.append(self.sink.context, bytes, quoted))
      return error;
    while (self.known && !bytes.empty()) {
      uint32_t scalar;
      auto length = self.converter.decode(
          &scalar,
          {reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()},
          self.output);
      if (!length || !*length) {
        self.known = false;
        break;
      }
      if (*length == size_t(-2))
        break;
      bytes.remove_prefix(*length);
    }
    return 0;
  }
  static int finish(void *context) {
    auto &self = *static_cast<Encoder *>(context);
    // A removed delimiter may have carried the reset for the preceding field.
    // Complete that field independently, without changing unsplit byte strings.
    if (self.split_boundary && self.known) {
      uint8_t suffix[MAX_LOCALE_STATE_PREFIX_BYTES];
      auto length = self.converter.transition({}, self.output, suffix);
      if (!length)
        return length.error();
      if (*length)
        if (int error = self.sink.append(
                self.sink.context,
                {reinterpret_cast<const char *>(suffix), *length}, false))
          return error;
    }
    int error = self.sink.finish(self.sink.context);
    self.output = {};
    self.known = true;
    self.split_boundary = false;
    return error;
  }
};
} // namespace

ErrorOr<size_t>
split_encoded_word_fields(cpp::span<const ExpansionPart> parts,
                          cpp::span<const FieldDelimiter> delimiters,
                          RestartableConverter converter, FieldSink sink) {
  if (!converter.state_dependent() || delimiters.empty())
    return split_word_fields(parts, delimiters, sink);
  if (!sink.append || !sink.finish)
    return Error(EINVAL);
  for (auto delimiter : delimiters)
    if (!delimiter.scalar || delimiter.bytes.empty())
      return Error(EINVAL);

  Encoder encoder{sink, converter};
  FieldSplitter fields({&encoder, Encoder::append, Encoder::finish});
  mbstate input{};
  bool known = true;
  for (const auto &part : parts) {
    bool split = part.split && !part.quoted;
    if (part.bytes.empty() && part.quoted) {
      encoder.required = input;
      encoder.character = 0;
      if (int error = fields.append({}, true))
        return Error(error);
    }
    auto bytes = part.bytes;
    bool continuation = !converter.boundary(input);
    while (!bytes.empty()) {
      encoder.required = input;
      encoder.character = 0;
      auto next = input;
      uint32_t scalar = 0;
      auto length = known
                        ? converter.decode(
                              &scalar,
                              {reinterpret_cast<const uint8_t *>(bytes.data()),
                               bytes.size()},
                              next)
                        : ErrorOr<size_t>(Error(EILSEQ));
      if (!length || !*length) {
        if (split)
          return Error(EILSEQ);
        if (int error = fields.append(bytes, part.quoted))
          return Error(error);
        known = false;
        break;
      }
      input = next;
      if (*length == size_t(-2)) {
        int error = converter.boundary(input)
                        ? fields.control(bytes, part.quoted)
                        : fields.append(bytes, part.quoted);
        if (error)
          return Error(error);
        break;
      }
      encoder.character = scalar;
      encoder.after = input;
      const FieldDelimiter *matched = nullptr;
      if (split && !continuation)
        for (const auto &delimiter : delimiters)
          if (scalar == delimiter.scalar) {
            matched = &delimiter;
            break;
          }
      if (matched)
        encoder.split_boundary = true;
      int error = matched
                      ? fields.delimiter(matched->whitespace)
                      : fields.append(bytes.substr(0, *length), part.quoted);
      if (error)
        return Error(error);
      bytes.remove_prefix(*length);
      continuation = false;
    }
  }
  return fields.complete();
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
