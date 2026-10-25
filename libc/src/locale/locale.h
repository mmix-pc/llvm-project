//===-- Implementation header for the locale --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_LOCALE_H
#define LLVM_LIBC_SRC_LOCALE_LOCALE_H

#include "src/__support/CPP/atomic.h"
#include "src/__support/CPP/optional.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"
#include "src/locale/collation_data.h"
#include "src/locale/ctype_data.h"

#include "hdr/locale_macros.h"
#include "hdr/types/locale_t.h"

#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {
namespace internal {
class LocaleDataFile;
}
} // namespace LIBC_NAMESPACE_DECL

// Private category records. Built-ins have no backing file and remain valid
// permanently; external records own a file reference and are shared by objects.
struct __locale_data {
  const char *name;
  bool utf8;
  LIBC_NAMESPACE::internal::LocaleDataFile *storage;
  LIBC_NAMESPACE::cpp::Atomic<unsigned> references{1};
  // Populated before publication; the view borrows this record's owned file.
  LIBC_NAMESPACE::cpp::optional<LIBC_NAMESPACE::internal::CollationData>
      collation;
  LIBC_NAMESPACE::cpp::optional<LIBC_NAMESPACE::internal::CTypeLocaleData> ctype;

  constexpr __locale_data(
      const char *name, bool utf8,
      LIBC_NAMESPACE::internal::LocaleDataFile *storage = nullptr)
      : name(name), utf8(utf8), storage(storage) {}
};

namespace LIBC_NAMESPACE_DECL {
namespace internal {
inline constexpr int LOCALE_CATEGORY_COUNT = 6;
inline constexpr int LOCALE_MASK = (1 << LOCALE_CATEGORY_COUNT) - 1;
// Six validated names of at most 255 bytes plus keys, separators and NUL.
inline constexpr size_t LOCALE_NAME_SIZE =
    LOCALE_CATEGORY_COUNT * (sizeof("LC_MONETARY") + 256);
// Thread selection borrows an object. The caller must keep it alive and must
// not modify it while any thread uses it. The global sentinel is not an object.
extern LIBC_THREAD_LOCAL locale_t thread_locale;
extern const char *const category_names[LOCALE_CATEGORY_COUNT];
__locale_t locale_from_mask(unsigned mask);
unsigned locale_mask(const __locale_t &loc);
__locale_t global_locale_snapshot();
// Acquires all new references before replacing the global state.
bool publish_global_locale(const __locale_t &loc);
// Ownership transfer primitive used only by publish_global_locale.
__locale_t exchange_global_locale(const __locale_t &loc);
__locale_t locale_snapshot(locale_t loc);
bool locale_is_utf8();
// Resolution is transactional. Applications synchronize environment mutation
// and setlocale with all operations affected by the global locale.
bool resolve_locale(int mask, const char *name, __locale_t &result);
// The returned thread-local buffer is reused by the next setlocale call.
char *locale_name(const __locale_t &loc, int category);
void format_locale_name(const __locale_t &loc, int category,
                        char (&buffer)[LOCALE_NAME_SIZE]);
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_LOCALE_H
