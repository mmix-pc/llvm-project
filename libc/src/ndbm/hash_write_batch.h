//===-- Staged BSD hash writes ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_HASH_WRITE_BATCH_H
#define LLVM_LIBC_SRC_NDBM_HASH_WRITE_BATCH_H

#include "hash_io.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

// The owner excludes concurrent access and supplies a validated original file.
// Preparation is discardable; publication is not a crash/rollback transaction.
class HashWriteBatch {
  struct Page {
    uint32_t number;
    Page *next;
    unsigned char bytes[HASH_PAGE_SIZE];
  };
  HashIO io;
  uint64_t original_size;
  uint64_t prepared_size;
  Page *pages = nullptr;
  bool started = false;

  static ErrorOr<size_t> read(void *, unsigned char *, size_t, uint64_t);

public:
  HashWriteBatch(HashIO io, uint64_t file_size)
      : io(io), original_size(file_size), prepared_size(file_size) {}
  ~HashWriteBatch();
  HashWriteBatch(const HashWriteBatch &) = delete;
  HashWriteBatch &operator=(const HashWriteBatch &) = delete;

  // Stage data or bitmap pages, never page zero's header. Copies input bytes.
  // Allocation failure preserves existing staged pages and the backing file.
  ErrorOr<size_t> stage(uint32_t physical,
                        const unsigned char (&bytes)[HASH_PAGE_SIZE]);
  HashIO view() { return {this, read, nullptr}; }
  uint64_t size() const { return prepared_size; }
  bool write_started() const { return started; }

  // Validate the complete prepared view before any write, then write pages,
  // the header and the optional owner-selected synchronization callback.
  // After the first write attempt the batch cannot be read, staged or retried.
  ErrorOr<bool> publish(const HashHeader &header,
                        ErrorOr<size_t> (*synchronize)(void *) = nullptr);
};

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_NDBM_HASH_WRITE_BATCH_H
