//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Internal utilities for environment variable management.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDLIB_ENVIRON_INTERNAL_H
#define LLVM_LIBC_SRC_STDLIB_ENVIRON_INTERNAL_H

#include "hdr/types/size_t.h"
#include "src/__support/CPP/optional.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Track allocations independently of the publicly writable environ slots.
// Startup strings and strings supplied by putenv are never added to this list.
struct EnvStringOwnership {
  char *string;
  size_t size;
};

// Centralized manager for environment variable operations.
// This class encapsulates all state and operations related to environment
// management, including memory management and tracking of string ownership.
//
// Read-only operations, including first use, do not modify shared state.
// Callers must synchronize environment mutations with all other accesses,
// including assignments to environ and edits to borrowed putenv strings.
class EnvironmentManager {
  // The last array we allocated, not necessarily the current environ. Never
  // cache a borrowed array: the application may replace and then free it.
  // Retain our last allocations across reads; the next successful mutation
  // reclaims unreachable strings and replaces this array, or clear frees both.
  char **storage = nullptr;

  // Independent allocation records, with the same capacity as storage.
  EnvStringOwnership *ownership = nullptr;
  size_t capacity = 0;
  size_t owned_count = 0;

  constexpr EnvironmentManager() = default;
  ~EnvironmentManager() = default;

  // Public environ is authoritative when supported, otherwise use app.env_ptr.
  static char **get_array();
  static void publish_array(char **array);

  // Search for a variable by name in the current environ array.
  // Returns the index if found, or nullopt if not found.
  cpp::optional<size_t> find_var(cpp::string_view name);

  // Ensure environ has capacity for at least `needed` entries (plus null
  // terminator). May allocate or reallocate storage. Returns true on
  // success, false on allocation failure.
  // Also reserve space for extra_owned new allocation records. No strings are
  // freed here: mutation arguments can themselves refer to owned strings.
  bool ensure_capacity(size_t needed, size_t extra_owned = 0);

  // After a successful mutation, free allocations no longer referenced by the
  // current array. Reads and failed mutations leave ownership unchanged.
  void reclaim_unused_strings(size_t count);

  // Result of a successful environment storage allocation.
  struct AllocResult {
    char **storage;
    EnvStringOwnership *ownership;
  };

  // Helper: allocate new storage and ownership arrays of the given capacity,
  // copy the current environment and the independent allocation records.
  // Returns nullopt on allocation failure; the old arrays are untouched.
  cpp::optional<AllocResult>
  alloc_and_copy(size_t new_capacity, char **old_storage, size_t copy_count);

public:
  // Get the singleton instance of the environment manager.
  static EnvironmentManager &get_instance();

  // Delete copy and move operations to enforce singleton pattern.
  EnvironmentManager(const EnvironmentManager &) = delete;
  EnvironmentManager &operator=(const EnvironmentManager &) = delete;
  EnvironmentManager(EnvironmentManager &&) = delete;
  EnvironmentManager &operator=(EnvironmentManager &&) = delete;

  // Iterator support for traversing environment entries.
  using iterator = char **;
  iterator begin();
  iterator end();
  size_t size() const;

  // Look up a variable by name. Returns a pointer to the value string
  // (after the '='), or nullptr if not found.
  char *get(cpp::string_view name);

  // Set or update an environment variable. Builds a "name=value" string,
  // manages ownership, and updates the environ array. If `overwrite` is
  // false and the variable already exists, does nothing and returns 0.
  // Returns 0 on success, -1 on allocation failure (caller should set
  // errno to ENOMEM).
  int set(cpp::string_view name, cpp::string_view value, bool overwrite);

  // Insert a caller-provided "name=value" string into the environment.
  // The caller retains ownership of caller-allocated strings. Reusing a
  // libc-owned string does not transfer ownership. If string contains no '=',
  // the named variable is removed
  // (POSIX behavior). Returns 0 on success, -1 on failure.
  int put(char *string);

  // Remove all matching variables, compact the array and reclaim unreferenced
  // owned strings. Returns 0 on success (including if the variable
  // was not found), -1 on allocation failure during array transition.
  int unset(cpp::string_view name);

  // Release managed storage without freeing startup or putenv strings.
  void clear();
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDLIB_ENVIRON_INTERNAL_H
