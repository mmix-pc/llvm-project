//===-- Standalone implementation of std::optional --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_CPP_OPTIONAL_H
#define LLVM_LIBC_SRC___SUPPORT_CPP_OPTIONAL_H

#include "src/__support/CPP/construct_at.h"
#include "src/__support/CPP/type_traits.h"
#include "src/__support/CPP/utility.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace cpp {

// Trivial nullopt_t struct.
struct nullopt_t {
  LIBC_INLINE constexpr explicit nullopt_t() = default;
};

// nullopt that can be used and returned.
LIBC_INLINE_VAR constexpr nullopt_t nullopt{};

// This is very simple implementation of the std::optional class. It makes
// several assumptions that the underlying type is trivially constructible,
// copyable, or movable.
template <typename T> class optional {
  template <typename U, bool = !is_trivially_destructible<U>::value>
  struct OptionalStorage {
    union {
      char empty;
      U stored_value;
    };

    bool in_use = false;

    LIBC_INLINE ~OptionalStorage() { reset(); }

    LIBC_INLINE constexpr OptionalStorage() : empty() {}

    template <typename... Args>
    LIBC_INLINE constexpr explicit OptionalStorage(in_place_t, Args &&...args)
        : stored_value(forward<Args>(args)...) {}

    LIBC_INLINE constexpr void reset() {
      if (in_use)
        stored_value.~U();
      in_use = false;
    }
  };

  // The only difference is that this type U doesn't have a nontrivial
  // destructor.
  template <typename U> struct OptionalStorage<U, false> {
    union {
      char empty;
      U stored_value;
    };

    bool in_use = false;

    LIBC_INLINE constexpr OptionalStorage() : empty() {}

    template <typename... Args>
    LIBC_INLINE constexpr explicit OptionalStorage(in_place_t, Args &&...args)
        : stored_value(forward<Args>(args)...) {}

    LIBC_INLINE constexpr void reset() { in_use = false; }
  };

  OptionalStorage<T> storage;

  template <typename U> LIBC_INLINE constexpr void assign_value(U &&value) {
    if (storage.in_use) {
      storage.stored_value = forward<U>(value);
    } else {
      if constexpr (is_trivially_copyable_v<T>) {
        // Union copy assignment starts the active member's lifetime and also
        // permits trivial value assignment in C++17 constant expressions.
        storage = OptionalStorage<T>(in_place, forward<U>(value));
      } else {
        cpp::construct_at(&storage.stored_value, forward<U>(value));
      }
      storage.in_use = true;
    }
  }

public:
  LIBC_INLINE constexpr optional() = default;
  LIBC_INLINE constexpr optional(nullopt_t) {}

  LIBC_INLINE constexpr optional(const T &t) : storage(in_place, t) {
    storage.in_use = true;
  }
  LIBC_INLINE constexpr optional(const optional &) = default;

  LIBC_INLINE constexpr optional(T &&t) : storage(in_place, move(t)) {
    storage.in_use = true;
  }
  LIBC_INLINE constexpr optional(optional &&O) = default;

  template <typename... ArgTypes>
  LIBC_INLINE constexpr optional(in_place_t, ArgTypes &&...Args)
      : storage(in_place, forward<ArgTypes>(Args)...) {
    storage.in_use = true;
  }

  LIBC_INLINE constexpr optional &operator=(T &&t) {
    assign_value(move(t));
    return *this;
  }
  LIBC_INLINE constexpr optional &operator=(optional &&) = default;

  LIBC_INLINE constexpr optional &operator=(const T &t) {
    assign_value(t);
    return *this;
  }
  LIBC_INLINE constexpr optional &operator=(const optional &) = default;

  LIBC_INLINE constexpr void reset() { storage.reset(); }

  LIBC_INLINE constexpr const T &value() const & {
    return storage.stored_value;
  }

  LIBC_INLINE constexpr T &value() & { return storage.stored_value; }

  LIBC_INLINE constexpr explicit operator bool() const {
    return storage.in_use;
  }
  LIBC_INLINE constexpr bool has_value() const { return storage.in_use; }
  LIBC_INLINE constexpr const T *operator->() const {
    return &storage.stored_value;
  }
  LIBC_INLINE constexpr T *operator->() { return &storage.stored_value; }
  LIBC_INLINE constexpr const T &operator*() const & {
    return storage.stored_value;
  }
  LIBC_INLINE constexpr T &operator*() & { return storage.stored_value; }

  LIBC_INLINE constexpr T &&value() && { return move(storage.stored_value); }
  LIBC_INLINE constexpr T &&operator*() && {
    return move(storage.stored_value);
  }

  template <typename U>
  LIBC_INLINE constexpr T value_or(U &&default_value) const & {
    return has_value() ? storage.stored_value
                       : static_cast<T>(forward<U>(default_value));
  }

  template <typename U> LIBC_INLINE constexpr T value_or(U &&default_value) && {
    return has_value() ? move(storage.stored_value)
                       : static_cast<T>(forward<U>(default_value));
  }
};

} // namespace cpp
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_CPP_OPTIONAL_H
