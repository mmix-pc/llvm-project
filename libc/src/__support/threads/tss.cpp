//===-- Common thread-specific values -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/tss.h"
#include "hdr/limits_macros.h"
#include "src/__support/CPP/array.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/CPP/optional.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/threads/mutex.h"
#include "src/__support/threads/thread.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
constexpr size_t TSS_KEY_COUNT = 1024;

struct TSSKeyUnit {
  // Indicates whether this unit is active. Presence of a non-null dtor
  // is not sufficient to indicate the same information as a TSS key can
  // have a null destructor.
  bool active = false;

  TSSDtor *dtor = nullptr;
  uint64_t generation = 0;

  void reset() {
    active = false;
    dtor = nullptr;
  }
};

class TSSKeyMgr {
  Mutex mtx;
  cpp::array<TSSKeyUnit, TSS_KEY_COUNT> units;

public:
  constexpr TSSKeyMgr()
      : mtx(/*is_priority_inherit=*/false, /*is_recursive=*/false,
            /*is_robust=*/false, /*is_pshared=*/false) {}

  cpp::optional<unsigned int> new_key(TSSDtor *dtor) {
    cpp::lock_guard lock(mtx);
    for (unsigned int i = 0; i < TSS_KEY_COUNT; ++i) {
      TSSKeyUnit &u = units[i];
      // Retire exhausted slots rather than reviving an ancient TLS value.
      if (!u.active && u.generation != cpp::numeric_limits<uint64_t>::max()) {
        ++u.generation;
        u.active = true;
        u.dtor = dtor;
        return i;
      }
    }
    return cpp::optional<unsigned int>();
  }

  cpp::optional<TSSKeyUnit> get_key(unsigned int key) {
    if (key >= TSS_KEY_COUNT)
      return cpp::nullopt;
    cpp::lock_guard lock(mtx);
    if (!units[key].active)
      return cpp::nullopt;
    return units[key];
  }

  bool remove_key(unsigned int key) {
    if (key >= TSS_KEY_COUNT)
      return false;
    cpp::lock_guard lock(mtx);
    if (!units[key].active)
      return false;
    units[key].reset();
    return true;
  }
};

TSSKeyMgr tss_key_mgr;

struct TSSValueUnit {
  void *payload = nullptr;
  uint64_t generation = 0;
};

static LIBC_THREAD_LOCAL cpp::array<TSSValueUnit, TSS_KEY_COUNT> tss_values;

} // namespace

namespace internal {
void call_tss_destructors() {
  for (unsigned pass = 0; pass < PTHREAD_DESTRUCTOR_ITERATIONS; ++pass) {
    bool called = false;
    for (unsigned i = 0; i < TSS_KEY_COUNT; ++i) {
      auto &unit = tss_values[i];
      if (!unit.payload)
        continue;
      auto key = tss_key_mgr.get_key(i);
      if (!key || key->generation != unit.generation) {
        unit = {};
        continue;
      }
      if (!key->dtor)
        continue;
      // Snapshot under the registry lock, but clear and invoke outside it.
      // Destructors may rearm values or create/delete keys.
      void *payload = unit.payload;
      unit = {};
      called = true;
      key->dtor(payload);
    }
    if (!called)
      break;
  }
  for (auto &unit : tss_values) {
    unit.payload = nullptr;
    unit.generation = 0;
  }
}
} // namespace internal

cpp::optional<unsigned int> new_tss_key(TSSDtor *dtor) {
  return tss_key_mgr.new_key(dtor);
}

bool tss_key_delete(unsigned int key) { return tss_key_mgr.remove_key(key); }

bool set_tss_value(unsigned int key, void *val) {
  auto entry = tss_key_mgr.get_key(key);
  if (!entry)
    return false;
  tss_values[key] = {val, entry->generation};
  return true;
}

void *get_tss_value(unsigned int key) {
  auto entry = tss_key_mgr.get_key(key);
  if (!entry)
    return nullptr;

  auto &u = tss_values[key];
  if (u.generation != entry->generation)
    return nullptr;
  return u.payload;
}

} // namespace LIBC_NAMESPACE_DECL
