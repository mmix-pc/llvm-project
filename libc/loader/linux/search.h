//===-- Startup dependency search --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_SEARCH_H
#define LLVM_LIBC_LOADER_LINUX_SEARCH_H

#include "loader/linux/mapping.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Strings are bounded readable bytes excluding the terminating NUL. Storage
// remains immutable throughout search. Origins are absolute directory names
// established from image provenance, never argv[0] or an implicit cwd fallback.
// An empty origin is unknown: search elements needing ORIGIN are skipped;
// a direct dependency needing it fails rather than changing its pathname.
struct SearchObject {
  Bytes origin, rpath, runpath;
  bool has_runpath;
};

class SearchEnvironment {
  Bytes library_path, origin, rejected;
  bool valid = false;

public:
  // Check secure admission before inspecting any environment string. The caller
  // must establish readable bounds; this is not arbitrary pointer recovery.
  long parse(bool secure, cpp::span<const Bytes> environment,
             Bytes main_origin);
  bool admitted() const { return valid; }
  Bytes paths() const { return library_path; }
  Bytes main_origin() const { return origin; }
  // The startup diagnostic can name a rejected setting (for example LD_PRELOAD).
  Bytes rejected_setting() const { return rejected; }
};

class DependencySearch {
public:
  static constexpr size_t PATH_CAPACITY = 4096;
  using CandidateLoader = MapResult (*)(void *, const char *);

  // Ancestry is nearest-parent first and excludes the requester. RUNPATH is
  // direct-only; a requester with RUNPATH suppresses inherited RPATH search.
  // Callback failure must consume its fd and roll back mappings, or report a
  // cleanup error that stops search immediately. Success retains the image.
  // Candidate pathname storage lasts only for the callback; copy it if retained.
  static MapResult resolve(Bytes name, const SearchObject &requester,
                           cpp::span<const SearchObject> ancestry,
                           const SearchEnvironment &environment,
                           CandidateLoader load, void *context);
  static MapResult load(Bytes name, const SearchObject &requester,
                        cpp::span<const SearchObject> ancestry,
                        const SearchEnvironment &environment, DsoMapping &out);
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_SEARCH_H
