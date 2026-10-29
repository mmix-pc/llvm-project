//===-- Linux file ownership for BSD hash databases --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NDBM_LINUX_HASH_FILE_H
#define LLVM_LIBC_SRC_NDBM_LINUX_HASH_FILE_H

#include "hdr/types/mode_t.h"
#include "src/ndbm/hash_write_batch.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

struct HashFileState {
  HashHeader header;
  uint64_t extent;
};

// The owner serializes all access, including through other handles/processes.
// A zero-length file is an empty database, initialized on its first mutation.
class HashFile {
  int fd = -1;
  int mode = 0;
  bool sync_writes = false;

  static ErrorOr<size_t> read(void *, unsigned char *, size_t, uint64_t);
  static ErrorOr<size_t> write(void *, const unsigned char *, size_t, uint64_t);
  static ErrorOr<size_t> sync(void *);

public:
  HashFile() = default;
  HashFile(const HashFile &) = delete;
  HashFile &operator=(const HashFile &) = delete;
  ~HashFile() { close(); }

  // Append .db without truncation. The descriptor remains owned on success,
  // including readonly empty-file creation. Any failure closes it exactly once.
  ErrorOr<HashFileState> open(const char *path, int flags, mode_t permissions);
  ErrorOr<HashFileState> inspect();
  ErrorOr<int> close();
  int descriptor() const { return fd; }
  bool readable() const;
  bool writable() const;
  HashIO io() { return {this, read, write}; }
  auto synchronizer() const -> ErrorOr<size_t> (*)(void *) {
    return sync_writes ? sync : nullptr;
  }

  static HashHeader empty_header();
  // Stage initial pages only for a zero-length file, before rebuilding buckets.
  static ErrorOr<size_t> prepare_empty(HashWriteBatch &batch);
};

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
#endif
