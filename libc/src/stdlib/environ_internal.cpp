//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation of internal environment management utilities.
///
//===----------------------------------------------------------------------===//

#include "src/stdlib/environ_internal.h"
#include "config/app.h"
#include "src/__support/CPP/new.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/macros/config.h"
#include "src/string/memory_utils/inline_memcpy.h"
#ifdef LIBC_COPT_SUPPORT_ENVIRON
#include "src/unistd/environ.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace internal {

constexpr size_t MIN_ENVIRON_CAPACITY = 32;
constexpr size_t ENVIRON_GROWTH_FACTOR = 2;

EnvironmentManager &EnvironmentManager::get_instance() {
  // Constant initialization; even the first lookup only reads shared state.
  static EnvironmentManager mgr;
  return mgr;
}

char **EnvironmentManager::get_array() {
#ifdef LIBC_COPT_SUPPORT_ENVIRON
  return environ;
#else
  return reinterpret_cast<char **>(app.env_ptr);
#endif
}

void EnvironmentManager::publish_array(char **array) {
  app.env_ptr = reinterpret_cast<uintptr_t *>(array);
#ifdef LIBC_COPT_SUPPORT_ENVIRON
  environ = array;
#endif
}

EnvironmentManager::iterator EnvironmentManager::begin() { return get_array(); }

EnvironmentManager::iterator EnvironmentManager::end() {
  char **array = get_array();
  if (array)
    while (*array)
      ++array;
  return array;
}

size_t EnvironmentManager::size() const {
  char **array = get_array();
  size_t count = 0;
  if (array)
    while (array[count])
      ++count;
  return count;
}

char *EnvironmentManager::get(cpp::string_view name) {
  auto idx = find_var(name);
  return idx ? get_array()[*idx] + name.size() + 1 : nullptr;
}

cpp::optional<size_t> EnvironmentManager::find_var(cpp::string_view name) {
  char **array = get_array();
  if (array) {
    for (size_t i = 0; array[i]; ++i) {
      cpp::string_view current(array[i]);
      if (current.starts_with(name) && current.size() > name.size() &&
          current[name.size()] == '=')
        return i;
    }
  }
  return cpp::nullopt;
}

cpp::optional<EnvironmentManager::AllocResult>
EnvironmentManager::alloc_and_copy(size_t new_capacity, char **old_storage,
                                   size_t copy_count) {
  AllocChecker ac;
  char **new_storage = new (ac) char *[new_capacity + 1];
  if (!ac)
    return cpp::nullopt;

  auto *new_ownership = new (ac) EnvStringOwnership[new_capacity];
  if (!ac) {
    delete[] new_storage;
    return cpp::nullopt;
  }

  for (size_t i = 0; i < copy_count; ++i)
    new_storage[i] = old_storage[i];
  new_storage[copy_count] = nullptr;
  for (size_t i = 0; i < owned_count; ++i)
    new_ownership[i] = ownership[i];

  return AllocResult{new_storage, new_ownership};
}

bool EnvironmentManager::ensure_capacity(size_t needed, size_t extra_owned) {
  if (extra_owned > size_t(-1) - owned_count)
    return false;
  if (needed < owned_count + extra_owned)
    needed = owned_count + extra_owned;

  char **array = get_array();
  if (storage && array == storage && needed <= capacity)
    return true;

  // Check both allocation sizes, including the environment's null terminator.
  constexpr size_t MAX_POINTERS = size_t(-1) / sizeof(char *) - 1;
  constexpr size_t MAX_RECORDS = size_t(-1) / sizeof(EnvStringOwnership);
  constexpr size_t MAX_CAPACITY =
      MAX_POINTERS < MAX_RECORDS ? MAX_POINTERS : MAX_RECORDS;
  if (needed > MAX_CAPACITY)
    return false;
  size_t new_capacity = needed <= MAX_CAPACITY / ENVIRON_GROWTH_FACTOR
                            ? needed * ENVIRON_GROWTH_FACTOR
                            : needed;
  if (new_capacity < MIN_ENVIRON_CAPACITY)
    new_capacity = MIN_ENVIRON_CAPACITY;

  auto result = alloc_and_copy(new_capacity, array, size());
  if (!result)
    return false;

  // Copy before freeing: environ may point into our previous array. No old
  // borrowed array or old public slot is consulted for ownership information.
  char **old_storage = storage;
  auto *old_ownership = ownership;
  storage = result->storage;
  ownership = result->ownership;
  capacity = new_capacity;
  publish_array(storage);
  delete[] old_storage;
  delete[] old_ownership;
  return true;
}

void EnvironmentManager::reclaim_unused_strings(size_t count) {
  size_t retained = 0;
  for (size_t i = 0; i < owned_count; ++i) {
    bool referenced = false;
    uintptr_t base = reinterpret_cast<uintptr_t>(ownership[i].string);
    for (size_t j = 0; j < count; ++j) {
      uintptr_t entry = reinterpret_cast<uintptr_t>(storage[j]);
      // putenv can also reuse a suffix of an owned "name=value" string.
      if (entry >= base && entry - base < ownership[i].size) {
        referenced = true;
        break;
      }
    }
    if (referenced)
      ownership[retained++] = ownership[i];
    else
      delete[] ownership[i].string;
  }
  owned_count = retained;
}

int EnvironmentManager::set(cpp::string_view name, cpp::string_view value,
                            bool overwrite) {
  auto idx = find_var(name);
  if (idx && !overwrite)
    return 0;

  size_t count = size();
  size_t name_len = name.size();
  size_t value_len = value.size();
  if (name_len > size_t(-1) - 2 || value_len > size_t(-1) - name_len - 2)
    return -1;
  size_t total_len = name_len + value_len + 2;
  AllocChecker ac;
  char *new_string = new (ac) char[total_len];
  if (!ac)
    return -1;
  inline_memcpy(new_string, name.data(), name_len);
  new_string[name_len] = '=';
  inline_memcpy(new_string + name_len + 1, value.data(), value_len);
  new_string[total_len - 1] = '\0';

  // Finish every fallible step before publishing or reclaiming anything.
  if (!ensure_capacity(idx ? count : count + 1, 1)) {
    delete[] new_string;
    return -1;
  }
  if (idx)
    storage[*idx] = new_string;
  else {
    storage[count++] = new_string;
    storage[count] = nullptr;
  }
  ownership[owned_count++] = {new_string, total_len};
  reclaim_unused_strings(count);
  return 0;
}

int EnvironmentManager::unset(cpp::string_view name) {
  auto idx = find_var(name);
  if (!idx)
    return 0;

  size_t count = size();
  if (!ensure_capacity(count))
    return -1;

  // Defer reclamation until all matches are removed: duplicates and even name
  // itself may refer to the same owned allocation.
  while (idx) {
    for (size_t j = *idx; j + 1 < count; ++j)
      storage[j] = storage[j + 1];
    storage[--count] = nullptr;
    idx = find_var(name);
  }
  reclaim_unused_strings(count);
  return 0;
}

void EnvironmentManager::clear() {
  publish_array(nullptr);
  for (size_t i = 0; i < owned_count; ++i)
    delete[] ownership[i].string;
  delete[] storage;
  delete[] ownership;
  storage = nullptr;
  ownership = nullptr;
  owned_count = capacity = 0;
}

int EnvironmentManager::put(char *string) {
  cpp::string_view sv(string);
  size_t eq_pos = sv.find_first_of('=');
  if (eq_pos == cpp::string_view::npos)
    return unset(sv);

  auto idx = find_var(sv.substr(0, eq_pos));
  size_t count = size();
  if (!ensure_capacity(idx ? count : count + 1))
    return -1;

  if (idx)
    storage[*idx] = string;
  else {
    storage[count++] = string;
    storage[count] = nullptr;
  }
  // Do not acquire ownership of caller memory. An existing libc allocation
  // reused by putenv remains owned and live until its last reference is gone.
  reclaim_unused_strings(count);
  return 0;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
