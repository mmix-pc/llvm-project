//===-- Runtime dependency transactions --------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RUNTIME_GRAPH_H
#define LLVM_LIBC_LOADER_LINUX_RUNTIME_GRAPH_H

#include "loader/linux/image_registry.h"
#include "loader/linux/search.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Private BFS closure. Registry images are borrowed; only new mappings and
// graph storage are owned here. The coordinator holds the mutation serializer
// and cancellation suppression throughout discovery/commit/rollback. No short
// registry lock spans file I/O. The input snapshot and environment stay pinned.
class RuntimeGraph {
public:
  class Node;
  struct Edge {
    const Node *target;
    const Edge *next;
  };
  class Node {
    friend class RuntimeGraph;
    DsoMapping mapping;
    const ImageRegistry::Image *resident = nullptr;
    FileIdentity file{};
    Bytes pathname;
    Node *parent = nullptr, *next_node = nullptr, *owned_next = nullptr;
    Edge *edges = nullptr, *edge_tail = nullptr;
    bool released = false;
    explicit Node(Address page) : mapping(page) {}

  public:
    const ElfImage *image() const {
      return resident ? &resident->elf : mapping.image();
    }
    const DynamicInfo *metadata() const {
      return resident ? resident->metadata() : mapping.metadata();
    }
    const ImageRegistry::Image *existing() const { return resident; }
    const FileIdentity &identity() const { return file; }
    Bytes path() const { return pathname; }
    const Node *load_parent() const { return parent; }
    const Node *next() const { return next_node; }
    const Edge *dependencies() const { return edges; }
  };

private:
  Address page_size;
  Arena arena;
  const ImageRegistry::Snapshot *base = nullptr;
  const ImageRegistry::Image *requester = nullptr;
  Node *owned = nullptr, *head = nullptr, *tail = nullptr;
  size_t count = 0, added = 0;
  bool storage_owned = false, ready = false, retained = false;
  Node *make_node(long &error);
  void append(Node &node);
  long borrow(const ImageRegistry::Image &, Node *&out);
  long edge(Node &source, Node &target);
  long ancestry(const Node *node, cpp::span<const SearchObject> &out);
  static SearchObject search_object(Bytes path, const DynamicInfo &info);
  struct Request {
    RuntimeGraph *graph;
    Node *parent;
    bool no_load;
    Node *found = nullptr;
    bool not_loaded = false;
  };
  static MapResult candidate(void *, const char *);
  MapResult resolve(Bytes name, Node *parent, const SearchEnvironment &, bool);
  MapResult discover(const SearchEnvironment &);

public:
  explicit RuntimeGraph(Address page) : page_size(page), arena(page) {}
  RuntimeGraph(const RuntimeGraph &) = delete;
  RuntimeGraph &operator=(const RuntimeGraph &) = delete;
  MapResult build(const ImageRegistry::Snapshot &, const ImageRegistry::Image &,
                  Bytes name, const SearchEnvironment &, bool no_load = false);
  const Node *scope() const { return ready ? head : nullptr; }
  size_t size() const { return ready ? count : 0; }
  size_t new_images() const { return ready ? added : 0; }
  // Attest that publication now borrows our mappings/metadata. The coordinator
  // must retain this object and its storage for process lifetime. This does not
  // perform relocation, TLS allocation or registry publication.
  bool retain();
  // Only after coordinated retirement/readers drain. A failed reset keeps
  // this node's mapping ownership; successful nodes are never reset twice.
  long release_mapping(Address load_bias);
  bool owns_mapping(Address load_bias) const;
  // Shared graph/search storage is released only after every owned mapping.
  long collect_storage();
  // Explicit rollback; failed unmaps retain ownership and block reuse. Never
  // unmap registry images. Published storage cannot be rolled back here.
  long reset();
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
