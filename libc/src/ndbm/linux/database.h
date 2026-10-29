//===-- Private file-backed DBM engine ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_LINUX_DATABASE_H
#define LLVM_LIBC_SRC_NDBM_LINUX_DATABASE_H

#include "hash_file.h"
#include "src/ndbm/hash_reader.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

class Database {
  HashFile file;
  HashHeader header{};
  uint64_t extent = 0;
  HashChainReader *cursor = nullptr;
  uint32_t bucket = 0;
  int failure = 0;
  bool damaged = false;
  unsigned char returned[HASH_RECORD_LIMIT + 1];

  Error error(int code) {
    failure = code;
    return Error(code);
  }
  ErrorOr<size_t> refresh();
  void reset_cursor();
  ErrorOr<bool> mutate(const HashRecord &record, bool replace, bool remove);

public:
  Database() = default;
  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;
  ~Database() { reset_cursor(); }

  ErrorOr<size_t> open(const char *path, int flags, mode_t permissions);
  ErrorOr<int> close();
  int error() const { return failure; }
  void clear_error() { failure = 0; }
  void record_error(int code) { failure = code; }

  // Successful results borrow storage until the next operation on this handle.
  // false means a missing key/end, not a transport or allocation failure.
  ErrorOr<bool> fetch(const void *key, size_t size, HashRecord &out);
  ErrorOr<bool> first(HashRecord &out);
  ErrorOr<bool> next(HashRecord &out);
  // true means inserted/replaced; false means an unchanged duplicate INSERT.
  ErrorOr<bool> store(const HashRecord &record, bool replace) {
    return mutate(record, replace, false);
  }
  ErrorOr<bool> erase(const void *key, size_t size) {
    return mutate({static_cast<const unsigned char *>(key), size, nullptr, 0},
                  false, true);
  }
};

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
#endif
