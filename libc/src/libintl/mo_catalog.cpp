//===-- Bounded GNU MO message catalog reader -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/mo_catalog.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/alloc-checker.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {

uint32_t word(const uint8_t *p, bool big) {
  uint32_t value = 0;
  for (unsigned i = 0; i != 4; ++i)
    value = (value << 8) | p[big ? i : 3 - i];
  return value;
}

bool table_range(size_t size, size_t offset, size_t count, size_t stride) {
  return offset <= size && count <= (size - offset) / stride;
}

// Compare unsigned bytes up to the singular msgid's first NUL. EOT is an
// ordinary byte here, not a separator to strip. Do not use signed-char ordering.
int compare_key(cpp::string_view a, cpp::string_view b) {
  size_t i = 0;
  for (;;) {
    unsigned left = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
    unsigned right = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
    if (left != right)
      return left < right ? -1 : 1;
    if (!left)
      return 0;
    ++i;
  }
}

bool space(char c) { return c == ' ' || c == '\t'; }

cpp::string_view trim(cpp::string_view value) {
  while (!value.empty() && space(value.front()))
    value.remove_prefix(1);
  while (!value.empty() && space(value.back()))
    value.remove_suffix(1);
  return value;
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

bool equal_name(cpp::string_view a, cpp::string_view b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i != a.size(); ++i)
    if (lower(a[i]) != lower(b[i]))
      return false;
  return true;
}

} // namespace

ErrorOr<MoCatalogView> MoCatalogView::parse(cpp::span<const uint8_t> bytes) {
  if (!bytes.data() || bytes.size() < HEADER_SIZE || bytes.size() > MAX_SIZE)
    return Error(EINVAL);
  const uint8_t *p = bytes.data();
  constexpr uint32_t MAGIC = 0x950412de;
  bool big = word(p, true) == MAGIC;
  if (!big && word(p, false) != MAGIC)
    return Error(EINVAL);
  // Reject both nonzero major and minor revisions, rather than silently
  // omitting revision 1 system-dependent messages.
  if (word(p + 4, big) != 0)
    return Error(ENOTSUP);
  size_t count = word(p + 8, big);
  size_t originals = word(p + 12, big), translations = word(p + 16, big);
  size_t hash_count = word(p + 20, big), hash_offset = word(p + 24, big);
  if (!table_range(bytes.size(), originals, count, 8) ||
      !table_range(bytes.size(), translations, count, 8) ||
      (hash_count && !table_range(bytes.size(), hash_offset, hash_count, 4)))
    return Error(EINVAL);
  // The optional accelerator is never used for lookup. Validate references,
  // but do not follow probe chains supplied by an untrusted producer.
  for (size_t i = 0; i != hash_count; ++i)
    if (word(p + hash_offset + 4 * i, big) > count)
      return Error(EINVAL);
  const size_t tables[] = {originals, translations};
  for (size_t table : tables) {
    for (size_t i = 0; i != count; ++i) {
      const uint8_t *entry = p + table + 8 * i;
      size_t length = word(entry, big), offset = word(entry + 4, big);
      // Subtract before adding the mandatory trailing NUL, including when a
      // hostile length is UINT32_MAX on a 32-bit host.
      if (offset >= bytes.size() || length >= bytes.size() - offset ||
          p[offset + length] != 0)
        return Error(EINVAL);
    }
  }
  // MO permits shared strings and suffixes. No disjoint-payload or native
  // alignment requirement is imposed on this immutable byte container.
  MoCatalogView view(bytes, count, originals, translations, big);
  for (size_t i = 1; i < count; ++i)
    if (compare_key(view.string(originals, i - 1),
                    view.string(originals, i)) >= 0)
      return Error(EINVAL);
  return view;
}

cpp::string_view MoCatalogView::string(size_t table, size_t index) const {
  const uint8_t *entry = bytes.data() + table + 8 * index;
  return {reinterpret_cast<const char *>(bytes.data() + word(entry + 4, big)),
          word(entry, big)};
}

ErrorOr<MoCatalogView::Message> MoCatalogView::at(size_t index) const {
  if (index >= count)
    return Error(EINVAL);
  return Message{string(originals, index), string(translations, index)};
}

ErrorOr<MoCatalogView::Message> MoCatalogView::find(cpp::string_view key) const {
  if ((!key.data() && !key.empty()) || key.size() > MAX_SIZE || key.contains('\0'))
    return Error(EINVAL);
  size_t low = 0, high = count;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    int order = compare_key(key, string(originals, mid));
    if (order < 0)
      high = mid;
    else if (order > 0)
      low = mid + 1;
    else
      return at(mid);
  }
  return Error(ENOENT);
}

ErrorOr<cpp::string_view> MoCatalogView::metadata() const {
  auto message = find({});
  if (!message)
    return Error(message.error());
  return message->translation;
}

ErrorOr<cpp::string_view>
MoCatalogView::header_field(cpp::string_view name) const {
  if (!name.data() || name.empty() || name.size() > MAX_SIZE ||
      name.contains('\0') || name.contains(':') || name.contains('\n') ||
      name.contains('\r'))
    return Error(EINVAL);
  auto header = metadata();
  if (!header)
    return Error(header.error());
  if (header->contains('\0'))
    return Error(EINVAL);
  bool found = false;
  cpp::string_view result;
  size_t pos = 0;
  while (pos < header->size()) {
    size_t end = header->find_first_of('\n', pos);
    if (end == cpp::string_view::npos)
      end = header->size();
    auto line = header->substr(pos, end - pos);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    size_t colon = line.find_first_of(':');
    if (colon != cpp::string_view::npos &&
        equal_name(trim(line.substr(0, colon)), name)) {
      if (found)
        return Error(EINVAL);
      result = trim(line.substr(colon + 1));
      found = true;
    }
    pos = end + 1;
  }
  if (!found)
    return Error(ENOENT);
  return result;
}

ErrorOr<cpp::string_view> MoCatalogView::charset() const {
  auto type = header_field("Content-Type");
  if (!type)
    return Error(type.error());
  bool found = false;
  cpp::string_view result;
  size_t pos = type->find_first_of(';');
  while (pos != cpp::string_view::npos) {
    size_t start = pos + 1;
    // Honor quoted parameters so a semicolon inside another parameter does
    // not manufacture a charset declaration.
    bool quoted = false, escaped = false;
    size_t end = start;
    for (; end < type->size(); ++end) {
      char c = (*type)[end];
      if (escaped)
        escaped = false;
      else if (quoted && c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = !quoted;
      else if (!quoted && c == ';')
        break;
    }
    if (quoted || escaped)
      return Error(EINVAL);
    auto parameter = trim(type->substr(start, end - start));
    size_t equal = parameter.find_first_of('=');
    if (equal != cpp::string_view::npos &&
        equal_name(trim(parameter.substr(0, equal)), "charset")) {
      if (found)
        return Error(EINVAL);
      result = trim(parameter.substr(equal + 1));
      if (result.size() >= 2 && result.front() == '"' && result.back() == '"')
        result = result.substr(1, result.size() - 2);
      if (result.empty())
        return Error(EINVAL);
      for (char c : result)
        if (static_cast<unsigned char>(c) <= 32 ||
            static_cast<unsigned char>(c) >= 127 || c == '"' || c == '\\' ||
            c == ';')
          return Error(EINVAL);
      found = true;
    }
    pos = end == type->size() ? cpp::string_view::npos : end;
  }
  if (!found)
    return Error(ENOENT);
  return result;
}

ErrorOr<cpp::string_view> MoCatalogView::plural_forms() const {
  return header_field("Plural-Forms");
}

ErrorOr<MoCatalog *> MoCatalog::copy(cpp::span<const uint8_t> bytes) {
  if (!bytes.data() || bytes.size() < MoCatalogView::HEADER_SIZE ||
      bytes.size() > MoCatalogView::MAX_SIZE)
    return Error(EINVAL);
  AllocChecker ac;
  uint8_t *storage = new (ac) uint8_t[bytes.size()];
  if (!ac)
    return Error(ENOMEM);
  for (size_t i = 0; i != bytes.size(); ++i)
    storage[i] = bytes[i];
  auto view = MoCatalogView::parse({storage, bytes.size()});
  if (!view) {
    ::free(storage);
    return Error(view.error());
  }
  auto *catalog = new (ac) MoCatalog(storage, view.value());
  if (!ac) {
    ::free(storage);
    return Error(ENOMEM);
  }
  return catalog;
}

bool MoCatalog::retain() {
  unsigned old = references.load(cpp::MemoryOrder::RELAXED);
  do {
    if (old == ~0U)
      return false;
  } while (!references.compare_exchange_weak(old, old + 1,
                                             cpp::MemoryOrder::RELAXED));
  return true;
}

MoCatalog::~MoCatalog() { ::free(storage); }

void MoCatalog::release() {
  if (references.fetch_sub(1, cpp::MemoryOrder::ACQ_REL) == 1) {
    this->~MoCatalog();
    ::free(this);
  }
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
