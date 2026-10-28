//===-- Validate complete BSD hash databases ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hash_database.h"
#include "hdr/errno_macros.h"
#include "src/__support/alloc-checker.h"
#include "src/stdlib/heap_sort.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {
namespace {
struct Key {
  const unsigned char *data;
  size_t size;
};

int compare(const Key &a, const Key &b) {
  size_t length = a.size < b.size ? a.size : b.size;
  for (size_t i = 0; i < length; ++i)
    if (a.data[i] != b.data[i])
      return a.data[i] < b.data[i] ? -1 : 1;
  return (a.size > b.size) - (a.size < b.size);
}

struct Workspace {
  Key *keys = nullptr;
  unsigned char *bytes = nullptr;
  ~Workspace() {
    delete[] bytes;
    delete[] keys;
  }
};
} // namespace

ErrorOr<size_t> validate_hash_database(const HashIO &io,
                                       const HashHeader &header,
                                       uint64_t file_size) {
  HashChainReader reader(io, header, file_size);
  auto validated = reader.validate_file();
  if (!validated)
    return Error(validated.error());

  // Equal keys necessarily hash to the same bucket. Bound temporary storage
  // by one bucket, and use heapsort rather than quadratic pairwise checking.
  for (uint32_t bucket = 0; bucket <= header.max_bucket; ++bucket) {
    auto start = reader.begin(bucket);
    if (!start)
      return Error(start.error());
    size_t count = 0, bytes = 0;
    HashRecord record{};
    for (;;) {
      auto next = reader.next(record);
      if (!next)
        return Error(next.error());
      if (!*next)
        break;
      if (++count > header.keys || record.key_size > SIZE_MAX - bytes)
        return Error(EOVERFLOW);
      bytes += record.key_size;
    }
    if (count < 2)
      continue;
    if (count > SIZE_MAX / sizeof(Key))
      return Error(EOVERFLOW);
    Workspace workspace;
    AllocChecker ac;
    workspace.keys = new (ac) Key[count];
    if (!ac)
      return Error(ENOMEM);
    workspace.bytes = new (ac) unsigned char[bytes ? bytes : 1];
    if (!ac)
      return Error(ENOMEM);

    start = reader.begin(bucket);
    if (!start)
      return Error(start.error());
    size_t index = 0, offset = 0;
    for (;;) {
      auto next = reader.next(record);
      if (!next)
        return Error(next.error());
      if (!*next)
        break;
      if (index == count || record.key_size > bytes - offset)
        return Error(EINVAL);
      workspace.keys[index++] = {workspace.bytes + offset, record.key_size};
      inline_memcpy(workspace.bytes + offset, record.key, record.key_size);
      offset += record.key_size;
    }
    if (index != count || offset != bytes)
      return Error(EINVAL);
    internal::ArrayGenericSize array(workspace.keys, count, sizeof(Key));
    internal::heap_sort(array, [](const void *a, const void *b) {
      return compare(*static_cast<const Key *>(a),
                     *static_cast<const Key *>(b)) < 0;
    });
    for (size_t i = 1; i < count; ++i)
      if (compare(workspace.keys[i - 1], workspace.keys[i]) == 0)
        return Error(EINVAL);
  }
  return *validated;
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
