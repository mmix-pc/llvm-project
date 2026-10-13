//===-- Startup dependency search -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/search.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static bool equal(Bytes value, const char *literal) {
  size_t i = 0;
  for (; literal[i]; ++i)
    if (i == value.size() || value[i] != literal[i])
      return false;
  return i == value.size();
}

static bool token_character(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

static long expand(Bytes input, Bytes origin, char *out) {
  size_t used = 0;
  for (size_t i = 0; i < input.size();) {
    if (!input[i])
      return -EINVAL;
    if (input[i] != '$') {
      if (used == DependencySearch::PATH_CAPACITY - 1)
        return -ENAMETOOLONG;
      out[used++] = input[i++];
      continue;
    }
    ++i;
    bool braces = i < input.size() && input[i] == '{';
    if (braces)
      ++i;
    size_t start = i;
    while (i < input.size() && token_character(input[i]))
      ++i;
    if (!equal(Bytes(input.data() + start, i - start), "ORIGIN") ||
        (braces && (i == input.size() || input[i++] != '}')))
      return -ENOTSUP;
    if (origin.empty() || origin[0] != '/')
      return -EINVAL;
    for (auto c : origin) {
      if (!c || c == '$')
        return -EINVAL;
      if (used == DependencySearch::PATH_CAPACITY - 1)
        return -ENAMETOOLONG;
      out[used++] = c;
    }
  }
  out[used] = 0;
  return 0;
}

// One traversal is used for both preflight and attempts. Preflight validates
// every applicable element, so a successful early candidate cannot hide an
// unsupported token in a later directory.
using PathVisitor = MapResult (*)(void *, const char *);
static MapResult visit_paths(Bytes list, Bytes origin, const char *name,
                             PathVisitor visit, void *context) {
  char path[DependencySearch::PATH_CAPACITY];
  for (size_t begin = 0; begin < list.size();) {
    size_t end = begin;
    while (end < list.size() && list[end] != ':')
      ++end;
    size_t start = begin;
    begin = end == list.size() ? end : end + 1;
    if (start == end)
      continue;
    long error = expand(Bytes(list.data() + start, end - start), origin, path);
    if (error)
      return {error, 0};
    size_t used = 0;
    while (path[used])
      ++used;
    if (used && path[used - 1] != '/') {
      if (used == sizeof(path) - 1)
        return {-ENAMETOOLONG, 0};
      path[used++] = '/';
    }
    for (size_t i = 0; name[i]; ++i) {
      if (used == sizeof(path) - 1)
        return {-ENAMETOOLONG, 0};
      path[used++] = name[i];
    }
    path[used] = 0;
    if (visit) {
      MapResult result = visit(context, path);
      if (result.error != -ENOENT || result.cleanup_error)
        return result;
    }
  }
  return {-ENOENT, 0};
}

long SearchEnvironment::parse(bool secure, cpp::span<const Bytes> env,
                              Bytes main_origin) {
  valid = false;
  library_path = {};
  origin = {};
  rejected = {};
  if (secure)
    return -ENOTSUP;
  bool seen = false;
  for (Bytes entry : env) {
    size_t split = 0;
    for (size_t i = 0; i < entry.size(); ++i)
      if (!entry[i])
        return -EINVAL;
    while (split < entry.size() && entry[split] != '=')
      ++split;
    Bytes key(entry.data(), split);
    Bytes value = split == entry.size() ? Bytes()
                                        : Bytes(entry.data() + split + 1,
                                                entry.size() - split - 1);
    if (equal(key, "LD_LIBRARY_PATH")) {
      if (seen) {
        rejected = key;
        return -EINVAL;
      }
      seen = true;
      library_path = value;
    } else if (key.size() >= 3 && key[0] == 'L' && key[1] == 'D' &&
               key[2] == '_' && !value.empty()) {
      rejected = key;
      return -ENOTSUP;
    }
  }
  MapResult checked =
      visit_paths(library_path, main_origin, "", nullptr, nullptr);
  if (checked.error != -ENOENT) {
    static constexpr unsigned char key[] = "LD_LIBRARY_PATH";
    rejected = Bytes(key, sizeof(key) - 1);
    return checked.error;
  }
  origin = main_origin;
  valid = true;
  return 0;
}

struct Attempt {
  DependencySearch::CandidateLoader load;
  void *context;
  bool denied = false;
};

static MapResult candidate(void *opaque, const char *path) {
  auto &attempt = *static_cast<Attempt *>(opaque);
  MapResult result = attempt.load(attempt.context, path);
  if (result.cleanup_error)
    return result;
  if (result.error == -EACCES) {
    attempt.denied = true;
    return {-ENOENT, 0};
  }
  if (result.error == -ENOTDIR)
    return {-ENOENT, 0};
  return result;
}

MapResult DependencySearch::resolve(Bytes name, const SearchObject &requester,
                                    cpp::span<const SearchObject> ancestry,
                                    const SearchEnvironment &environment,
                                    CandidateLoader load, void *context) {
  if (!environment.admitted() || !load || name.empty())
    return {-EINVAL, 0};
  char expanded[PATH_CAPACITY];
  long error = expand(name, requester.origin, expanded);
  if (error)
    return {error, 0};
  bool pathname = false;
  for (size_t i = 0; expanded[i]; ++i)
    pathname |= expanded[i] == '/';
  if (pathname)
    return load(context, expanded);

  Attempt attempt{load, context};
  // Validate the full selected search scope before the first mapping attempt.
  for (unsigned pass = 0; pass != 2; ++pass) {
    PathVisitor visit = pass ? candidate : nullptr;
    if (!requester.has_runpath) {
      MapResult result = visit_paths(requester.rpath, requester.origin,
                                     expanded, visit, &attempt);
      if (result.error != -ENOENT || result.cleanup_error)
        return result;
      for (const auto &parent : ancestry) {
        if (parent.has_runpath)
          continue;
        result =
            visit_paths(parent.rpath, parent.origin, expanded, visit, &attempt);
        if (result.error != -ENOENT || result.cleanup_error)
          return result;
      }
    }
    MapResult result =
        visit_paths(environment.paths(), environment.main_origin(), expanded,
                    visit, &attempt);
    if (result.error != -ENOENT || result.cleanup_error)
      return result;
    if (requester.has_runpath) {
      result = visit_paths(requester.runpath, requester.origin, expanded, visit,
                           &attempt);
      if (result.error != -ENOENT || result.cleanup_error)
        return result;
    }
    static constexpr unsigned char defaults[] = "/lib:/usr/lib";
    result = visit_paths(Bytes(defaults, sizeof(defaults) - 1), {}, expanded,
                         visit, &attempt);
    if (result.error != -ENOENT || result.cleanup_error)
      return result;
  }
  return {attempt.denied ? -EACCES : -ENOENT, 0};
}

MapResult DependencySearch::load(Bytes name, const SearchObject &requester,
                                 cpp::span<const SearchObject> ancestry,
                                 const SearchEnvironment &environment,
                                 DsoMapping &out) {
  return resolve(
      name, requester, ancestry, environment,
      [](void *context, const char *path) {
        return static_cast<DsoMapping *>(context)->open(path);
      },
      &out);
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
