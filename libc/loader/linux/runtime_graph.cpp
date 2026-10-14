//===-- Runtime dependency transactions -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_graph.h"
#include "hdr/errno_macros.h"
#include "loader/linux/syscall.h"
#include "loader/linux/target.h"
#include "src/__support/CPP/new.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {

RuntimeGraph::Node *RuntimeGraph::make_node(long &error) {
  void *storage;
  error = arena.allocate(sizeof(Node), alignof(Node), storage);
  if (error)
    return nullptr;
  storage_owned = true;
  auto *node = new (storage) Node(page_size);
  node->owned_next = owned;
  owned = node;
  return node;
}

void RuntimeGraph::append(Node &node) {
  if (tail)
    tail->next_node = &node;
  else
    head = &node;
  tail = &node;
  ++count;
}

long RuntimeGraph::borrow(const ImageRegistry::Image &image, Node *&out) {
  for (Node *node = head; node; node = node->next_node)
    if (node->resident == &image) {
      out = node;
      return 0;
    }
  long error;
  Node *node = make_node(error);
  if (!node)
    return error;
  if (DynamicInfo::open(image.elf, node->dynamic) != DynamicError::None)
    return -ENOEXEC;
  node->resident = &image;
  node->file = image.identity;
  node->pathname = image.path;
  append(*node);
  out = node;
  return 0;
}

long RuntimeGraph::edge(Node &source, Node &target) {
  void *storage;
  long error = arena.allocate(sizeof(Edge), alignof(Edge), storage);
  if (error)
    return error;
  auto *link = new (storage) Edge{&target, nullptr};
  if (source.edge_tail)
    source.edge_tail->next = link;
  else
    source.edges = link;
  source.edge_tail = link;
  return 0;
}

SearchObject RuntimeGraph::search_object(Bytes path, const DynamicInfo &info) {
  size_t end = path.size();
  while (end && path[end - 1] != '/')
    --end;
  SearchObject object{{path.data(), end > 1 ? end - 1 : end},
                      {},
                      {},
                      info.has(DynamicTag::Runpath)};
  if (info.has(DynamicTag::Rpath))
    info.string(info.value(DynamicTag::Rpath), object.rpath);
  if (object.has_runpath)
    info.string(info.value(DynamicTag::Runpath), object.runpath);
  return object;
}

long RuntimeGraph::ancestry(const Node *node,
                            cpp::span<const SearchObject> &out) {
  size_t depth = 0;
  for (const Node *p = node ? node->parent : nullptr; p; p = p->parent)
    ++depth;
  // A new root was requested by the published caller. Its descendants inherit
  // that caller's first-discovery ancestry, not arbitrary dependency
  // back-edges.
  for (const auto *p = node ? requester : requester->parent; p; p = p->parent)
    ++depth;
  out = {};
  if (!depth)
    return 0;
  if (depth > __SIZE_MAX__ / sizeof(SearchObject))
    return -EOVERFLOW;
  void *storage;
  long error = arena.allocate(depth * sizeof(SearchObject),
                              alignof(SearchObject), storage);
  if (error)
    return error;
  storage_owned = true;
  auto *objects = static_cast<SearchObject *>(storage);
  size_t index = 0;
  for (const Node *p = node ? node->parent : nullptr; p; p = p->parent)
    new (objects + index++)
        SearchObject(search_object(p->path(), *p->metadata()));
  for (const auto *p = node ? requester : requester->parent; p; p = p->parent) {
    DynamicInfo info;
    if (DynamicInfo::open(p->elf, info) != DynamicError::None)
      return -ENOEXEC;
    new (objects + index++) SearchObject(search_object(p->path, info));
  }
  out = {objects, depth};
  return 0;
}

MapResult RuntimeGraph::candidate(void *context, const char *path) {
  auto &request = *static_cast<Request *>(context);
  auto &graph = *request.graph;
  char absolute[DependencySearch::PATH_CAPACITY];
  size_t used = 0;
  if (path[0] != '/') {
    long result = raw_syscall(SYS_getcwd, reinterpret_cast<long>(absolute),
                              sizeof(absolute));
    if (result < 0)
      return {result, 0};
    if (result < 2 || static_cast<size_t>(result) > sizeof(absolute) ||
        absolute[0] != '/' || absolute[result - 1])
      return {-EIO, 0};
    used = result - 1;
    if (absolute[used - 1] != '/') {
      if (used == sizeof(absolute) - 1)
        return {-ENAMETOOLONG, 0};
      absolute[used++] = '/';
    }
  }
  for (size_t i = 0; path[i]; ++i) {
    if (used == sizeof(absolute) - 1)
      return {-ENAMETOOLONG, 0};
    absolute[used++] = path[i];
  }
  absolute[used] = 0;
  OwnedFd fd;
  long error = fd.open_readonly(absolute);
  if (error)
    return {error, 0};
  FileIdentity identity;
  error = fd.identity(identity);
  if (error)
    return {error, fd.close()};
  for (const auto *image : graph.base->images) {
    if (image->identity_known && image->identity == identity) {
      error = fd.close();
      if (!error)
        error = graph.borrow(*image, request.found);
      return {error, 0};
    }
  }
  for (Node *node = graph.head; node; node = node->next_node) {
    if (!node->resident && node->file == identity) {
      error = fd.close();
      if (!error)
        request.found = node;
      return {error, 0};
    }
  }
  if (request.no_load) {
    request.not_loaded = true;
    // Stop search at the first existing file, without mapping or trying a
    // lower-priority directory. Resolve translates this private sentinel.
    return {-EAGAIN, fd.close()};
  }
  Node *node = graph.make_node(error);
  if (!node)
    return {error, fd.close()};
  void *storage;
  error = graph.arena.allocate(used, 1, storage);
  if (error)
    return {error, fd.close()};
  auto *copy = static_cast<unsigned char *>(storage);
  for (size_t i = 0; i < used; ++i)
    copy[i] = absolute[i];
  node->pathname = {copy, used};
  MapResult result = node->mapping.open(fd);
  if (result.error || result.cleanup_error) {
    long cleanup = fd.close();
    if (!result.cleanup_error)
      result.cleanup_error = cleanup;
    return result;
  }
  node->file = identity;
  node->parent = request.parent;
  graph.append(*node);
  ++graph.added;
  request.found = node;
  return {0, 0};
}

MapResult RuntimeGraph::resolve(Bytes name, Node *parent,
                                const SearchEnvironment &environment,
                                bool no_load) {
  cpp::span<const SearchObject> ancestors;
  long error = ancestry(parent, ancestors);
  if (error)
    return {error, 0};
  DynamicInfo info;
  if (!parent && DynamicInfo::open(requester->elf, info) != DynamicError::None)
    return {-ENOEXEC, 0};
  auto object = parent ? search_object(parent->path(), *parent->metadata())
                       : search_object(requester->path, info);
  Request request{this, parent, no_load};
  MapResult result = DependencySearch::resolve(
      name, object, ancestors, environment, candidate, &request);
  if (request.not_loaded)
    result.error = -ENOENT;
  if (!result.error && !result.cleanup_error && parent)
    result.error = edge(*parent, *request.found);
  return result;
}

MapResult RuntimeGraph::discover(const SearchEnvironment &environment) {
  for (Node *node = head; node; node = node->next_node) {
    if (node->resident) {
      // Reopening cannot rebind dependencies or search a changed filesystem.
      for (const auto *image : node->resident->dependencies) {
        Node *target;
        long error = borrow(*image, target);
        if (!error)
          error = edge(*node, *target);
        if (error)
          return {error, 0};
      }
      continue;
    }
    for (Address i = 0; i < node->metadata()->needed_count(); ++i) {
      Bytes name;
      if (!node->metadata()->needed(i, name))
        return {-ENOEXEC, 0};
      MapResult result = resolve(name, node, environment, false);
      if (result.error || result.cleanup_error)
        return result;
    }
  }
  return {0, 0};
}

MapResult RuntimeGraph::build(const ImageRegistry::Snapshot &snapshot,
                              const ImageRegistry::Image &caller, Bytes name,
                              const SearchEnvironment &environment,
                              bool no_load) {
  if (storage_owned || owned || retained)
    return {-EBUSY, 0};
  if (!is_supported_page_size(page_size) || !environment.admitted())
    return {-EINVAL, 0};
  bool found = false;
  for (const auto *image : snapshot.images) {
    if (!image || !image->discovered || !image->elf.is_mapped())
      return {-EINVAL, 0};
    found |= image == &caller;
  }
  if (!found)
    return {-EINVAL, 0};
  base = &snapshot;
  requester = &caller;
  MapResult result = resolve(name, nullptr, environment, no_load);
  if (!result.error && !result.cleanup_error)
    result = discover(environment);
  if (result.error || result.cleanup_error) {
    long cleanup = reset();
    if (!result.cleanup_error)
      result.cleanup_error = cleanup;
    return result;
  }
  ready = true;
  return {0, 0};
}

bool RuntimeGraph::retain() {
  if (!ready || retained)
    return false;
  retained = true;
  return true;
}

long RuntimeGraph::reset() {
  if (retained)
    return -EBUSY;
  ready = false;
  long error = 0;
  for (Node *node = owned; node; node = node->owned_next) {
    long cleanup = node->mapping.reset();
    if (!error)
      error = cleanup;
  }
  if (error)
    return error;
  owned = head = tail = nullptr;
  count = added = 0;
  base = nullptr;
  requester = nullptr;
  error = arena.reset();
  storage_owned = error != 0;
  return error;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
