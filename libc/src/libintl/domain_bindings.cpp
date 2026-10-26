//===-- Translation domain binding storage --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/domain_bindings.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/threads/mutex.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
Mutex binding_mutex(false, false, false, false);
char default_domain[] = "messages";
char default_directory[] = "/usr/share/locale";
char *current_domain = default_domain;

struct Binding {
  Binding *next;
  char *name;
  char *directory;
  char *codeset;
};
// Entries remain reachable for the process lifetime; replacements free only
// the changed string, preserving other domains and the other binding field.
Binding *bindings = nullptr;

char *copy(cpp::string_view value) {
  if (value.size() == cpp::numeric_limits<size_t>::max())
    return nullptr;
  AllocChecker ac;
  auto *result = new (ac) char[value.size() + 1];
  if (!ac)
    return nullptr;
  for (size_t i = 0; i < value.size(); ++i)
    result[i] = value[i];
  result[value.size()] = '\0';
  return result;
}
} // namespace

bool try_lock_domain_bindings_for_fork() {
  return binding_mutex.try_lock() == MutexError::NONE;
}

void unlock_domain_bindings_after_fork() {
  if (binding_mutex.unlock() != MutexError::NONE)
    __builtin_trap();
}

ErrorOr<char *> current_text_domain(const char *name) {
  cpp::lock_guard guard(binding_mutex);
  if (!name)
    return current_domain;
  if (!*name)
    name = default_domain;
  if (cpp::string_view(name) == current_domain)
    return current_domain;
  char *replacement = name == default_domain ? default_domain : copy(name);
  if (!replacement)
    return Error(ENOMEM);
  if (current_domain != default_domain)
    ::free(current_domain);
  current_domain = replacement;
  return current_domain;
}

ErrorOr<char *> bind_text_domain(const char *name, const char *value,
                                 DomainBinding kind) {
  if (!name || !*name)
    return static_cast<char *>(nullptr);
  // Empty binding values are unspecified; reject without changing state.
  if (value && !*value)
    return Error(EINVAL);
  cpp::lock_guard guard(binding_mutex);
  Binding *entry = bindings;
  for (; entry; entry = entry->next)
    if (cpp::string_view(entry->name) == name)
      break;
  char *old = entry ? (kind == DomainBinding::DIRECTORY ? entry->directory
                                                        : entry->codeset)
                    : nullptr;
  if (!value)
    return old                                ? old
           : kind == DomainBinding::DIRECTORY ? default_directory
                                              : nullptr;
  if (old && cpp::string_view(old) == value)
    return old;
  char *replacement = copy(value);
  if (!replacement)
    return Error(ENOMEM);
  if (!entry) {
    char *saved_name = copy(name);
    if (!saved_name) {
      ::free(replacement);
      return Error(ENOMEM);
    }
    AllocChecker ac;
    entry = new (ac) Binding{bindings, saved_name, nullptr, nullptr};
    if (!ac) {
      ::free(saved_name);
      ::free(replacement);
      return Error(ENOMEM);
    }
    bindings = entry;
  }
  if (kind == DomainBinding::DIRECTORY)
    entry->directory = replacement;
  else
    entry->codeset = replacement;
  ::free(old);
  return replacement;
}

DomainSnapshot::~DomainSnapshot() { ::free(storage); }

ErrorOr<void> snapshot_text_domain(const char *name, DomainSnapshot &result) {
  cpp::lock_guard guard(binding_mutex);
  if (!name)
    name = current_domain;
  else if (!*name)
    name = default_domain;
  Binding *entry = bindings;
  for (; entry; entry = entry->next)
    if (cpp::string_view(entry->name) == name)
      break;
  const char *directory =
      entry && entry->directory ? entry->directory : default_directory;
  const char *codeset = entry ? entry->codeset : nullptr;
  cpp::string_view values[] = {name, directory, codeset ? codeset : ""};
  size_t size = 0;
  for (auto value : values) {
    if (value.size() >= cpp::numeric_limits<size_t>::max() - size)
      return Error(EOVERFLOW);
    size += value.size() + 1;
  }
  AllocChecker ac;
  char *storage = new (ac) char[size];
  if (!ac)
    return Error(ENOMEM);
  char *positions[3];
  char *output = storage;
  for (unsigned i = 0; i != 3; ++i) {
    positions[i] = output;
    for (char c : values[i])
      *output++ = c;
    *output++ = 0;
  }
  ::free(result.storage);
  result.storage = storage;
  result.domain_value = positions[0];
  result.directory_value = positions[1];
  result.codeset_value = codeset ? positions[2] : nullptr;
  return {};
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
