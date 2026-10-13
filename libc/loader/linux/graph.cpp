//===-- Startup dependency graph ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/graph.h"
#include "hdr/errno_macros.h"
#include "loader/linux/symbols.h"
#include "loader/linux/syscall.h"
#include "loader/linux/target.h"
#include "src/__support/CPP/new.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {

long StartupSymbols::open(const StartupGraph &graph, Arena &arena,
                          StartupSymbols &out) {
  out.objects = {};
  if (!graph.scope() || !graph.size())
    return -EINVAL;
  if (graph.size() > __SIZE_MAX__ / sizeof(SymbolObject))
    return -EOVERFLOW;
  void *storage;
  long error = arena.allocate(graph.size() * sizeof(SymbolObject),
                              alignof(SymbolObject), storage);
  if (error)
    return error;
  auto *views = static_cast<SymbolObject *>(storage);
  size_t i = 0;
  for (const auto *node = graph.scope(); node; node = node->next()) {
    if (i == graph.size() || !node->fully_discovered())
      return -EINVAL;
    new (views + i++) SymbolObject{node->image(), node->metadata()};
  }
  if (i != graph.size())
    return -EINVAL;
  return open({views, i}, out) == SymbolError::None ? 0 : -EINVAL;
}

StartupGraph::Node *StartupGraph::make_node(long &error) {
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

long StartupGraph::set_path(Node &node, Bytes path) {
  if (path.empty() || path[0] != '/')
    return -EINVAL;
  if (path.size() >= DependencySearch::PATH_CAPACITY)
    return -ENAMETOOLONG;
  for (auto c : path)
    if (!c)
      return -EINVAL;
  void *storage;
  long error = arena.allocate(path.size(), 1, storage);
  if (error)
    return error;
  auto *out = static_cast<unsigned char *>(storage);
  for (size_t i = 0; i < path.size(); ++i)
    out[i] = path[i];
  node.pathname = Bytes(out, path.size());
  return 0;
}

SearchObject StartupGraph::search_object(const Node &node) {
  size_t end = node.pathname.size();
  while (end && node.pathname[end - 1] != '/')
    --end;
  Bytes origin(node.pathname.data(), end > 1 ? end - 1 : end);
  const DynamicInfo &info = *node.metadata();
  SearchObject object{origin, {}, {}, info.has(DynamicTag::Runpath)};
  if (info.has(DynamicTag::Rpath))
    info.string(info.value(DynamicTag::Rpath), object.rpath);
  if (object.has_runpath)
    info.string(info.value(DynamicTag::Runpath), object.runpath);
  return object;
}

MapResult StartupGraph::candidate(void *context, const char *path) {
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
        absolute[0] != '/' || absolute[result - 1] != 0)
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
  for (Node *node = graph.head; node; node = node->scope_next) {
    if (node->identity == identity) {
      error = fd.close();
      if (!error)
        request.found = node;
      return {error, 0};
    }
  }
  Node *node = graph.make_node(error);
  if (!node)
    return {error, fd.close()};
  error = graph.set_path(
      *node, Bytes(reinterpret_cast<unsigned char *>(absolute), used));
  if (error)
    return {error, fd.close()};
  MapResult result = node->mapping.open(fd);
  if (result.error || result.cleanup_error) {
    long close_error = fd.close();
    if (!result.cleanup_error)
      result.cleanup_error = close_error;
    return result;
  }
  node->identity = identity;
  node->parent = request.parent;
  graph.tail->scope_next = node;
  graph.tail = node;
  ++graph.count;
  request.found = node;
  return {0, 0};
}

MapResult StartupGraph::discover(const SearchEnvironment &environment) {
  // Appending newly discovered nodes while advancing this list is the BFS
  // queue. Parent links form the first-discovery tree, not dependency edges.
  for (Node *node = head; node; node = node->scope_next) {
    size_t depth = 0;
    for (Node *parent = node->parent; parent; parent = parent->parent)
      ++depth;
    cpp::span<const SearchObject> ancestry;
    if (depth) {
      if (depth > __SIZE_MAX__ / sizeof(SearchObject))
        return {-EOVERFLOW, 0};
      void *storage;
      long error = arena.allocate(depth * sizeof(SearchObject),
                                  alignof(SearchObject), storage);
      if (error)
        return {error, 0};
      auto *objects = static_cast<SearchObject *>(storage);
      size_t index = 0;
      for (Node *parent = node->parent; parent; parent = parent->parent)
        new (objects + index++) SearchObject(search_object(*parent));
      ancestry = cpp::span<const SearchObject>(objects, depth);
    }
    for (Address i = 0; i < node->metadata()->needed_count(); ++i) {
      Bytes name;
      if (!node->metadata()->needed(i, name))
        return {-ENOEXEC, 0};
      Request request{this, node};
      MapResult result =
          DependencySearch::resolve(name, search_object(*node), ancestry,
                                    environment, candidate, &request);
      if (result.error || result.cleanup_error)
        return result;
      void *storage;
      long error = arena.allocate(sizeof(Edge), alignof(Edge), storage);
      if (error)
        return {error, 0};
      auto *edge = new (storage) Edge{request.found, nullptr};
      if (node->edge_tail)
        node->edge_tail->next = edge;
      else
        node->edges = edge;
      node->edge_tail = edge;
    }
    node->scanned = true;
  }
  return {0, 0};
}

MapResult StartupGraph::build(const ElfImage &main, FileIdentity identity,
                              Bytes path,
                              const SearchEnvironment &environment) {
  if (storage_owned || owned)
    return {-EBUSY, 0};
  if (!is_supported_page_size(page_size) || !main.is_mapped() ||
      (main.role() != ImageRole::MainExecutable &&
       main.role() != ImageRole::MainPIE) ||
      !environment.admitted())
    return {-EINVAL, 0};
  long error;
  Node *root = make_node(error);
  if (!root)
    return {error, reset()};
  root->borrowed = &main;
  root->identity = identity;
  error = set_path(*root, path);
  if (!error &&
      DynamicInfo::open(main, root->main_dynamic) != DynamicError::None)
    error = -ENOEXEC;
  if (error)
    return {error, reset()};
  head = tail = root;
  count = 1;
  MapResult result = discover(environment);
  if (result.error || result.cleanup_error) {
    long cleanup = reset();
    if (!result.cleanup_error)
      result.cleanup_error = cleanup;
    return result;
  }
  ready = true;
  return {0, 0};
}

long StartupGraph::reset() {
  ready = false;
  long error = 0;
  for (Node *node = owned; node; node = node->owned_next) {
    long result = node->mapping.reset();
    if (!error)
      error = result;
  }
  if (error)
    return error;
  // Descriptor storage must outlive any retained mapping cleanup obligation.
  owned = head = tail = nullptr;
  count = 0;
  error = arena.reset();
  storage_owned = error != 0;
  return error;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
