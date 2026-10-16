//===-- Resident image registry tests -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "elf_fixture.h"
#include "dynamic_fixture.h"
#include "loader/linux/image_registry.h"
#include "loader/linux/syscall.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace loader_test;
static bool fail_map, fail_unmap;
static int maps_before_failure = -1;
static unsigned mappings, unmappings;

static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    fprintf(stderr, "image registry failure at %d\n", line);
    abort();
  }
}

namespace LIBC_NAMESPACE::loader {
long raw_syscall(long number, long a1, long a2, long a3, long a4, long a5,
                 long a6) {
  if (number == SYS_mmap) {
    if (fail_map || maps_before_failure == 0)
      return -ENOMEM;
    if (maps_before_failure > 0)
      --maps_before_failure;
    long result = ::syscall(number, a1, a2, a3, a4, a5, a6);
    if (result == -1)
      return -errno;
    ++mappings;
    return result;
  }
  check(number == SYS_munmap);
  if (fail_unmap)
    return -EIO;
  if (::munmap(reinterpret_cast<void *>(a1), a2))
    return -errno;
  ++unmappings;
  return 0;
}
} // namespace LIBC_NAMESPACE::loader

struct Input {
  Fixture file;
  ElfImage image;
  explicit Input(ImageRole role = ImageRole::SharedObject) : file(role) {
    file.segment(7, PT_TLS, PF_R, 0x4808, 0x4808, 8, 24, 64);
    check(ElfImage::open_file(file.view(), role, bias, page, image) ==
          ElfError::None);
  }
};

static void ready(ImageRegistry::Transaction &transaction) {
  check(!transaction.mark_tls_ready());
  check(!transaction.mark_protected());
  check(transaction.mark_relocated());
  check(!transaction.mark_relocated());
  check(transaction.mark_tls_ready());
  check(transaction.mark_protected());
  check(!transaction.mark_protected());
}

extern "C" int main() {
  const size_t host_page = ::sysconf(_SC_PAGESIZE);
  Input main(ImageRole::MainPIE), initial, late, other, last;
  const ElfImage *startup[] = {&main.image, &initial.image};
  const ElfImage *batch[] = {&late.image};
  {
    ImageRegistry metadata(host_page);
    ImageRegistry::Transaction transaction(host_page);
    unsigned char path[] = {'/', 'a'};
    size_t edges[1024] = {};
    ImageRegistry::Discovery discovery[] = {
        {true, {1, 1, 0}, {path, sizeof(path)}, ~size_t(0), edges},
        {true, {2, 1, 0}, {}, 0, {}}};
    unsigned before = mappings;
    check(!metadata.prepare(transaction, startup, discovery));
    unsigned required = mappings - before;
    check(required > 1);
    path[1] = 'b';
    edges[0] = 1;
    const auto *view = transaction.view();
    check(view->images[0]->path[1] == 'a');
    check(view->images[0]->dependencies[0] == view->images[0]);
    check(view->images[1]->parent == view->images[0]);
    check(!transaction.abort());
    for (unsigned fail = 0; fail < required; ++fail) {
      maps_before_failure = fail;
      check(metadata.prepare(transaction, startup, discovery) == -ENOMEM);
      check(!transaction.view() && !metadata.snapshot());
      maps_before_failure = -1;
      check(!transaction.abort() && mappings == unmappings);
    }
  }
  ImageRegistry registry(host_page);
  ImageRegistry::Transaction first(host_page);
  check(!registry.snapshot() && !first.view());
  check(registry.publish(first) == -EINVAL);
  const ElfImage *invalid[] = {nullptr};
  check(registry.prepare(first, invalid) == -EINVAL);
  check(registry.prepare(first, {}) == -EINVAL);
  check(registry.prepare(first, batch) == -EINVAL);
  check(registry.prepare(first, startup) == 0);
  check(!registry.snapshot() && first.view()->generation == 1);
  check(first.view()->images[0]->tls.module_id == 1);
  check(first.abort() == 0);
  check(registry.prepare(first, startup) == 0);
  check(first.view()->images[0]->tls.module_id == 1);
  check(first.view()->images[0]->tls.tp_offset == 72);
  check(first.view()->images[1]->tls.tp_offset == 136);
  check(registry.publish(first) == -EINVAL);
  ready(first);
  ImageRegistry foreign(host_page);
  check(foreign.publish(first) == -EINVAL);
  check(registry.publish(first) == 0);
  const auto *old = registry.snapshot();
  check(first.abort() == -EBUSY && registry.publish(first) == -EINVAL);
  check(registry.prepare(first, batch) == -EBUSY);
  check(old->find(1, 1) == old->images[0]);
  check(old->images[0]->ownership.is_permanent());
  check(old->images[1]->ownership.is_permanent());
  check(!old->find(1, 2) && !old->find(0, 0));

  ImageRegistry::Transaction a(host_page), b(host_page);
  check(registry.prepare(a, startup) == -EINVAL);
  const ElfImage *duplicate[] = {&initial.image};
  check(registry.prepare(a, duplicate) == -EEXIST);
  const ElfImage *repeated[] = {&late.image, &late.image};
  check(registry.prepare(a, repeated) == -EEXIST);
  check(registry.prepare(a, batch) == 0);
  uint64_t discarded_id = a.view()->images[2]->tls.module_id;
  uint64_t discarded_generation = a.view()->generation;
  check(a.view()->images[0] == old->images[0]);
  check(a.view()->images[2]->tls.tp_offset == 0);
  check(!a.view()->images[2]->startup);
  check(!a.view()->images[2]->ownership.is_permanent());
  check(a.view()->images[2]->tls.alignment == 64);
  check(a.view()->images[2]->tls.residue == 8);
  check(registry.snapshot() == old && old->images.size() == 2);
  const ElfImage *second[] = {&other.image};
  check(registry.prepare(b, second) == 0);
  ready(b);
  check(registry.publish(b) == 0);
  ready(a);
  check(registry.publish(a) == -EAGAIN);
  check(!registry.snapshot()->find(discarded_id, discarded_generation));
  check(a.abort() == 0);
  check(registry.prepare(a, batch) == 0);
  check(a.view()->images[3]->tls.module_id > discarded_id);
  check(a.view()->generation > b.view()->generation);
  ready(a);
  check(registry.publish(a) == 0);
  check(old->images.size() == 2 && old->images[0]->tls.tp_offset == 72);
  check(b.view()->images.size() == 3 &&
        registry.snapshot()->images.size() == 4);

  ImageRegistry::Transaction failed(host_page);
  batch[0] = &last.image;
  fail_map = true;
  const auto *before = registry.snapshot();
  check(registry.prepare(failed, batch) == -ENOMEM);
  check(!failed.view() && registry.snapshot() == before);
  check(registry.prepare(failed, batch) == -EBUSY);
  fail_map = false;
  check(failed.abort() == 0);
  check(registry.prepare(failed, batch) == 0);
  fail_unmap = true;
  check(failed.abort() == -EIO && !failed.view());
  check(registry.prepare(failed, batch) == -EBUSY);
  check(registry.snapshot() == before);
  fail_unmap = false;
  check(failed.abort() == 0);

  Input many[20];
  const ElfImage *large[20];
  for (unsigned i = 0; i < 20; ++i)
    large[i] = &many[i].image;
  maps_before_failure = 1;
  unsigned allocated = mappings, released_partial = unmappings;
  check(registry.prepare(failed, large) == -ENOMEM);
  check(mappings == allocated + 1 && !failed.view());
  check(registry.snapshot() == before);
  maps_before_failure = -1;
  check(failed.abort() == 0 && unmappings == released_partial + 1);

  ImageRegistry::Transaction mismatch(host_page * 2);
  check(registry.prepare(mismatch, batch) == 0);
  ready(mismatch);
  check(registry.publish(mismatch) == -EINVAL);
  check(registry.snapshot() == before && mismatch.abort() == 0);

  // Transfer preserves views and removes only the source's ownership.
  Arena source(host_page), destination(host_page), wrong(host_page * 2);
  void *p;
  check(source.allocate(16, 8, p) == 0);
  static_cast<char *>(p)[0] = 42;
  check(!wrong.adopt(source) && !source.adopt(source));
  unsigned released = unmappings;
  check(destination.adopt(source));
  check(source.reset() == 0 && unmappings == released);
  check(static_cast<char *>(p)[0] == 42);
  check(destination.reset() == 0 && unmappings == released + 1);
  check(mappings > unmappings); // Published registry storage is resident.

  RegistrySequence ids;
  uint64_t id = 99;
  check(!ids.reserve(0, id) && id == 99);
  check(ids.reserve(~uint64_t(0) - 1, id) && id == 1);
  check(!ids.reserve(2, id) && id == 1);
  check(ids.reserve(1, id) && id == ~uint64_t(0));
  check(!ids.can_reserve(1) && !ids.reserve(1, id));

  DynamicFixture pinned;
  ElfImage pinned_image;
  check(ElfImage::open_file(pinned.view(), ImageRole::SharedObject, bias, page,
                            pinned_image) == ElfError::None);
  const ElfImage *pinned_batch[] = {&pinned_image};
  ImageRegistry::Transaction nodelete(host_page);
  check(!registry.prepare(nodelete, pinned_batch));
  auto *pinned_record = nodelete.view()->images[nodelete.view()->images.size() - 1];
  check(!pinned_record->startup && pinned_record->ownership.is_permanent());
  check(pinned_record->ownership.retire() == -EBUSY);
  check(!nodelete.abort());

  // Edge reachability, rather than incoming-reference counts, collects cycles.
  Input ga, gb, provider, consumer;
  const ElfImage *graph_images[] = {&ga.image, &gb.image, &provider.image,
                                   &consumer.image};
  const size_t first_new = registry.snapshot()->images.size();
  size_t ab[] = {first_new + 1, first_new + 1};
  size_t ba[] = {first_new};
  ImageRegistry::Discovery graph_info[] = {
      {false, {}, {}, ~size_t(0), ab},
      {false, {}, {}, ~size_t(0), ba},
      {},
      {false, {}, {}, ~size_t(0), ba}};
  ImageRegistry::Transaction graph_tx(host_page);
  check(!registry.prepare(graph_tx, graph_images, graph_info));
  auto *graph_view = graph_tx.view();
  const auto *a_record = graph_view->images[first_new];
  const auto *b_record = graph_view->images[first_new + 1];
  const auto *p_record = graph_view->images[first_new + 2];
  const auto *c_record = graph_view->images[first_new + 3];
  check(a_record->dependencies.size() == 1);
  check(!registry.record_binding(graph_tx, a_record, p_record));
  const auto *edge = a_record->bindings;
  check(edge && edge->provider == p_record && !edge->next);
  check(!registry.record_binding(graph_tx, a_record, p_record) &&
        a_record->bindings == edge);
  check(!registry.record_binding(graph_tx, a_record, a_record));
  check(registry.record_binding(graph_tx, old->images[0], a_record) == -EINVAL);
  check(registry.record_binding(graph_tx, a_record, nullptr) == -EINVAL);
  ImageRegistry::Image outsider{};
  check(registry.record_binding(graph_tx, a_record, &outsider) == -EINVAL);

  // Limit this view to the subgraph, so earlier unrelated late records do not
  // obscure the set under test. Production snapshots retain all live images.
  const ImageRegistry::Image *subgraph[] = {a_record, b_record, p_record, c_record};
  ImageRegistry::Snapshot graph_snapshot{graph_view->generation, subgraph};
  Arena scratch(host_page), tickets(host_page);
  LIBC_NAMESPACE::cpp::span<const ImageRegistry::Image *const> candidates;
  check(!graph_snapshot.reclaimable(scratch, {}, candidates) && candidates.size() == 4);
  check(!scratch.reset());
  const ImageRegistry::Image *root[] = {c_record};
  check(!graph_snapshot.reclaimable(scratch, root, candidates) && candidates.empty());
  check(!scratch.reset());
  root[0] = a_record;
  check(!graph_snapshot.reclaimable(scratch, root, candidates) &&
        candidates.size() == 1 && candidates[0] == c_record);
  check(!scratch.reset());
  ImageRegistry::Image permanent{{}, {}, true};
  {
    ImageRegistry::Image ancestor{}, child{};
    child.parent = &ancestor;
    const ImageRegistry::Image *ancestry[] = {&ancestor, &child};
    const ImageRegistry::Image *child_root[] = {&child};
    ImageRegistry::Snapshot ancestry_view{1, ancestry};
    check(!ancestry_view.reclaimable(scratch, child_root, candidates) &&
          candidates.empty());
    check(!scratch.reset());
    const ImageRegistry::Image *removed_parent[] = {&ancestor};
    ImageRegistry::Snapshot *replacement;
    check(ancestry_view.without(scratch, removed_parent, replacement) == -EBUSY);
    child.parent = &outsider;
    check(ancestry_view.reclaimable(scratch, {}, candidates) == -EINVAL);
    check(!scratch.reset());
  }
  const ImageRegistry::Image *permanent_edges[] = {a_record};
  permanent.dependencies = permanent_edges;
  const ImageRegistry::Image *rooted[] = {
      &permanent, a_record, b_record, p_record, c_record};
  ImageRegistry::Snapshot permanent_graph{1, rooted};
  check(!permanent_graph.reclaimable(scratch, {}, candidates) &&
        candidates.size() == 1 && candidates[0] == c_record);
  check(!scratch.reset());
  ModuleOwnership::Token lease;
  check(!p_record->ownership.acquire(tickets, ModuleOwnership::Kind::Runtime, lease));
  check(!graph_snapshot.reclaimable(scratch, {}, candidates) && candidates.size() == 3);
  for (const auto *entry : candidates)
    check(entry != p_record);
  check(!scratch.reset());
  check(!p_record->ownership.release(lease) && !tickets.reset());
  fail_map = true;
  check(graph_snapshot.reclaimable(scratch, {}, candidates) == -ENOMEM && candidates.empty());
  fail_map = false;
  check(!scratch.reset());
  root[0] = &outsider;
  check(graph_snapshot.reclaimable(scratch, root, candidates) == -EINVAL);
  check(graph_tx.mark_relocated());
  check(registry.record_binding(graph_tx, b_record, p_record) == -EINVAL);
  check(!graph_tx.abort() && !old->images[0]->bindings);

  // Inject a failure while growing the private provider-edge ledger.
  ImageRegistry::Transaction edge_failure(host_page);
  constexpr size_t edge_count = 64;
  auto *edge_inputs = static_cast<Input *>(::mmap(
      nullptr, sizeof(Input) * edge_count, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  check(edge_inputs != MAP_FAILED);
  const ElfImage *edge_images[edge_count];
  for (size_t i = 0; i < edge_count; ++i) {
    new (edge_inputs + i) Input;
    edge_images[i] = &edge_inputs[i].image;
  }
  check(!registry.prepare(edge_failure, edge_images));
  const auto *edge_view = edge_failure.view();
  const size_t begin = edge_view->images.size() - edge_count;
  fail_map = true;
  bool saw_failure = false;
  for (size_t i = begin; i < edge_view->images.size() && !saw_failure; ++i) {
    const auto *requester = edge_view->images[i];
    for (size_t j = begin; j < edge_view->images.size(); ++j) {
      const auto *previous = requester->bindings;
      long error = registry.record_binding(edge_failure, requester, edge_view->images[j]);
      if (error) {
        check(error == -ENOMEM && requester->bindings == previous);
        saw_failure = true;
        break;
      }
    }
  }
  check(saw_failure);
  fail_map = false;
  check(!edge_failure.abort() && !old->images[0]->bindings);
  for (size_t i = 0; i < edge_count; ++i)
    edge_inputs[i].~Input();
  check(!::munmap(edge_inputs, sizeof(Input) * edge_count));
  return 0;
}
