//===-- Stable translated message storage ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/message_results.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/alloc-checker.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

struct MessageResults::Entry {
  Entry *next;
  char *value;
  size_t size;
};

ErrorOr<char *> MessageResults::insert(char *value, size_t size) {
  struct Owner {
    char *data;
    ~Owner() { ::free(data); }
  } owner{value};
  size_t hash = 0;
  for (size_t i = 0; i != size; ++i)
    hash = hash * 33 + static_cast<unsigned char>(value[i]);
  cpp::lock_guard guard(mutex);
  Entry *&bucket = buckets[hash % 256];
  for (Entry *entry = bucket; entry; entry = entry->next)
    if (cpp::string_view(entry->value, entry->size) ==
        cpp::string_view(value, size))
      return entry->value;
  AllocChecker ac;
  auto *entry = new (ac) Entry{bucket, value, size};
  if (!ac)
    return Error(ENOMEM);
  bucket = entry;
  owner.data = nullptr;
  return value;
}

void MessageResults::clear() {
  cpp::lock_guard guard(mutex);
  for (Entry *&bucket : buckets) {
    while (bucket) {
      Entry *entry = bucket;
      bucket = entry->next;
      ::free(entry->value);
      ::free(entry);
    }
  }
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
