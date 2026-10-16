//===-- MMIX shared pthread key ownership ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/tss.h"
#include "hdr/limits_macros.h"
#include "loader_cancellation.h"
#include "src/__support/CPP/array.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/threads/mutex.h"
#include "src/__support/threads/thread.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
constexpr unsigned KEY_COUNT = 1024;

class CancellationScope {
  loader::LoaderCancellation hooks = mmix::loader_cancellation_hooks();
  uint64_t saved = 0;

public:
  CancellationScope() {
    if (hooks.enter(&saved))
      __builtin_trap();
  }
  ~CancellationScope() noexcept(false) {
    if (hooks.leave(saved))
      __builtin_trap();
  }
};

struct Registration {
  TSSDtor *callback;
  uint64_t pin;
  // The table's reference and acquired invocation references are distinct.
  // All share one loader ticket until the final reference is dropped.
  bool registered = true;
  uint64_t invocations = 0;
};
struct Key {
  Registration *registration = nullptr;
  uint64_t generation = 0;
};
struct Value {
  void *payload = nullptr;
  uint64_t generation = 0;
};
Mutex mutex(false, false, false, false);
cpp::array<Key, KEY_COUNT> keys;
LIBC_THREAD_LOCAL cpp::array<Value, KEY_COUNT> values;

// No loader lock or allocator call is made under the key registry mutex.
void destroy(Registration *record) {
  if (record->pin && mmix::release_shared_tls_destructor(record->pin))
    __builtin_trap();
  LIBC_NAMESPACE::free(record);
}

void release_invocation(Registration *record) {
  bool last;
  {
    cpp::lock_guard lock(mutex);
    if (!record->invocations)
      __builtin_trap();
    --record->invocations;
    last = !record->registered && !record->invocations;
  }
  if (last)
    destroy(record);
}
} // namespace

cpp::optional<unsigned int> new_tss_key(TSSDtor *callback) {
  CancellationScope scope;
  uint64_t pin = 0;
  if (callback && mmix::retain_shared_tls_destructor(
                      nullptr, reinterpret_cast<uintptr_t>(callback), &pin))
    return cpp::nullopt;
  void *memory = LIBC_NAMESPACE::malloc(sizeof(Registration));
  if (!memory) {
    if (pin && mmix::release_shared_tls_destructor(pin))
      __builtin_trap();
    return cpp::nullopt;
  }
  auto *record = new (memory) Registration{callback, pin};
  {
    cpp::lock_guard lock(mutex);
    for (unsigned i = 0; i != KEY_COUNT; ++i) {
      auto &key = keys[i];
      if (!key.registration &&
          key.generation != cpp::numeric_limits<uint64_t>::max()) {
        ++key.generation;
        key.registration = record;
        return i;
      }
    }
  }
  destroy(record);
  return cpp::nullopt;
}

bool tss_key_delete(unsigned int key) {
  if (key >= KEY_COUNT)
    return false;
  CancellationScope scope;
  Registration *record;
  bool last;
  {
    cpp::lock_guard lock(mutex);
    record = keys[key].registration;
    if (!record)
      return false;
    keys[key].registration = nullptr;
    record->registered = false;
    last = !record->invocations;
  }
  if (last)
    destroy(record);
  return true;
}

bool set_tss_value(unsigned int key, void *value) {
  if (key >= KEY_COUNT)
    return false;
  CancellationScope scope;
  cpp::lock_guard lock(mutex);
  if (!keys[key].registration)
    return false;
  values[key] = {value, keys[key].generation};
  return true;
}

void *get_tss_value(unsigned int key) {
  if (key >= KEY_COUNT)
    return nullptr;
  CancellationScope scope;
  cpp::lock_guard lock(mutex);
  if (!keys[key].registration || values[key].generation != keys[key].generation)
    return nullptr;
  return values[key].payload;
}

namespace internal {
void call_tss_destructors() {
  CancellationScope scope;
  for (unsigned pass = 0; pass < PTHREAD_DESTRUCTOR_ITERATIONS; ++pass) {
    bool called = false;
    for (unsigned i = 0; i != KEY_COUNT; ++i) {
      Registration *record;
      void *payload;
      {
        cpp::lock_guard lock(mutex);
        auto &value = values[i];
        if (!value.payload)
          continue;
        record = keys[i].registration;
        if (!record || value.generation != keys[i].generation) {
          value = {};
          continue;
        }
        if (!record->callback)
          continue;
        if (record->invocations == cpp::numeric_limits<uint64_t>::max())
          __builtin_trap();
        ++record->invocations;
        payload = value.payload;
        value = {};
      }
      // Deletion/reuse can proceed now, but cannot release this callback's
      // code. Rearmed values are considered again on the next bounded pass.
      called = true;
      record->callback(payload);
      release_invocation(record);
    }
    if (!called)
      break;
  }
  for (auto &value : values)
    value = {};
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
