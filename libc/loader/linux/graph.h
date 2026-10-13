//===-- Startup dependency graph ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_GRAPH_H
#define LLVM_LIBC_LOADER_LINUX_GRAPH_H

#include "loader/linux/search.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

class StartupGraph {
public:
  class Node;
  struct Edge {
    const Node *target;
    const Edge *next;
  };
  class Node {
    friend class StartupGraph;
    DsoMapping mapping;
    const ElfImage *borrowed = nullptr;
    DynamicInfo main_dynamic;
    FileIdentity identity;
    Bytes pathname;
    Node *parent = nullptr, *scope_next = nullptr, *owned_next = nullptr;
    Edge *edges = nullptr, *edge_tail = nullptr;
    bool scanned = false;
    explicit Node(Address page_size) : mapping(page_size) {}

  public:
    const ElfImage *image() const {
      return borrowed ? borrowed : mapping.image();
    }
    const DynamicInfo *metadata() const {
      return borrowed ? &main_dynamic : mapping.metadata();
    }
    Bytes path() const { return pathname; }
    const Node *next() const { return scope_next; }
    const Edge *dependencies() const { return edges; }
    bool fully_discovered() const { return scanned; }
  };

private:
  Address page_size;
  Arena arena;
  Node *owned = nullptr, *head = nullptr, *tail = nullptr;
  bool storage_owned = false, ready = false, protected_images = false;
  size_t count = 0;
  Node *make_node(long &error);
  long set_path(Node &node, Bytes path);
  static SearchObject search_object(const Node &node);
  struct Request {
    StartupGraph *graph;
    Node *parent;
    Node *found = nullptr;
  };
  static MapResult candidate(void *context, const char *path);
  MapResult discover(const SearchEnvironment &environment);

public:
  explicit StartupGraph(Address page_size)
      : page_size(page_size), arena(page_size) {}
  StartupGraph(const StartupGraph &) = delete;
  StartupGraph &operator=(const StartupGraph &) = delete;
  // Borrow the admitted kernel main image and its independently established
  // file identity/path. No main mapping is adopted or released. Input metadata
  // and environment stay immutable; startup is serialized with no cwd changes.
  MapResult build(const ElfImage &main, FileIdentity identity, Bytes path,
                  const SearchEnvironment &environment);
  // Explicit lifetime, as for DsoMapping. Failed cleanup blocks reuse and keeps
  // storage alive for retry; all node/edge views are invalid after reset.
  long reset();
  // Caller has completed all relocations. Failure invalidates the graph and
  // resets owned DSOs, never the borrowed main; cleanup errors remain visible.
  MapResult finalize_protections();
  const Node *scope() const { return ready ? head : nullptr; }
  size_t size() const { return ready ? count : 0; }
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_GRAPH_H
