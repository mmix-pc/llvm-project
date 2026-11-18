//===-- Eager startup relocation ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/relocation.h"
#include "loader/linux/target.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

class RelocationDestinations {
  const ElfImage &image;
  cpp::span<const WritableRegion> writable;
  const WritableRegion *verified = nullptr;

public:
  RelocationDestinations(const ElfImage &image,
                         cpp::span<const WritableRegion> writable)
      : image(image), writable(writable) {}

  unsigned char *get(Address offset) {
    if (offset % 8)
      return nullptr;
    // A whole-range match ties every aligned destination in it to the same
    // live writable mapping. The cache belongs to this object's preflight.
    if (verified && offset >= verified->vaddr &&
        offset - verified->vaddr <= verified->bytes.size() - 8)
      return verified->bytes.data() + (offset - verified->vaddr);

    Bytes expected = image.mapped_memory_bytes(offset, 8, PF_R | PF_W);
    if (expected.size() != 8)
      return nullptr;
    for (const auto &region : writable) {
      if (!region.bytes.data() || region.bytes.size() < 8 ||
          offset < region.vaddr ||
          offset - region.vaddr > region.bytes.size() - 8)
        continue;
      unsigned char *p = region.bytes.data() + (offset - region.vaddr);
      if (p != expected.data())
        continue;
      // Borrowed callers may supply a range that is valid only at this
      // destination. Retain their per-record fallback in that case.
      Bytes entire = image.mapped_memory_bytes(region.vaddr, region.bytes.size(),
                                                PF_R | PF_W);
      if (entire.data() == region.bytes.data())
        verified = &region;
      return p;
    }
    return nullptr;
  }
};

// Bounded, transaction-local memoization. Scope snapshots are shared by readers
// and must remain immutable; only this relocation call owns the cache.
class RelocationSymbols {
  const StartupSymbols &symbols;
  struct Entry {
    size_t requester = 0;
    Address index = 0;
    SymbolValue value;
  } entries[64];

public:
  explicit RelocationSymbols(const StartupSymbols &symbols) : symbols(symbols) {}
  SymbolError resolve(size_t requester, Address index, SymbolValue &out) {
    auto &entry = entries[(index ^ requester) % 64];
    if (index && entry.index == index && entry.requester == requester) {
      __builtin_memcpy_inline(&out, &entry.value, sizeof(out));
      return SymbolError::None;
    }
    SymbolError error = symbols.resolve(requester, index, out);
    if (error == SymbolError::None) {
      entry.requester = requester;
      entry.index = index;
      __builtin_memcpy_inline(&entry.value, &out, sizeof(out));
    }
    return error;
  }
};

static StartupRelocationError value(const SymbolObject &object,
                                    RelocationSymbols &symbols, size_t index,
                                    const DynamicRelocation &r, Address &out,
                                    const StartupTLSSet *tls) {
  if (is_tls_relocation(r.info)) {
    if (!tls)
      return StartupRelocationError::Value;
    size_t provider = index;
    Address offset = 0;
    if (r.info >> 32) {
      SymbolValue symbol;
      if (symbols.resolve(index, r.info >> 32, symbol) != SymbolError::None ||
          !symbol.tls || symbol.weak_zero)
        return StartupRelocationError::Symbol;
      provider = symbol.object;
      offset = symbol.address;
    }
    const auto &module = tls->modules[provider];
    return tls_relocation_value(r.info, module.module_id, module.tp_offset,
                                module.memory_size, offset, r.addend, out)
               ? StartupRelocationError::None
               : StartupRelocationError::Value;
  }
  Address base = object.image->load_bias();
  if (!is_relative_relocation(r.info)) {
    SymbolValue symbol;
    if (symbols.resolve(index, r.info >> 32, symbol) != SymbolError::None ||
        symbol.tls)
      return StartupRelocationError::Symbol;
    base = symbol.address;
  }
  return runtime_relocation_value(r.info, base, r.addend, out)
             ? StartupRelocationError::None
             : StartupRelocationError::Value;
}

static void write_relocation(unsigned char *destination, Address value) {
  if constexpr (__BYTE_ORDER__ != __ORDER_BIG_ENDIAN__)
    value = __builtin_bswap64(value);
  using Octa __attribute__((may_alias)) = Address;
  if (!(reinterpret_cast<uintptr_t>(destination) % alignof(Octa)))
    *reinterpret_cast<Octa *>(destination) = value;
  else
    __builtin_memcpy_inline(destination, &value, sizeof(value));
}

StartupRelocationResult
relocate_objects(cpp::span<const SymbolObject> objects,
                 cpp::span<const cpp::span<const WritableRegion>> writable,
                 RelocationValue resolve, void *context,
                 cpp::span<RelocationWrite> scratch) {
  if (objects.size() != writable.size() || !resolve)
    return {StartupRelocationError::Image, 0, 0};
  size_t relative_end = 0, symbolic_begin = scratch.size();
  for (size_t i = 0; i < objects.size(); ++i) {
    const auto &object = objects[i];
    if (!object.image || !object.metadata || !object.image->is_mapped() ||
        object.image->role() == ImageRole::Interpreter)
      return {StartupRelocationError::Image, i, 0};
    // Only owned, already validated metadata paired with this image may
    // omit the live recheck. Destination and value preflight still run for
    // every record before any write.
    if (object.metadata->owns_metadata() &&
        !object.metadata->is_stable_for(*object.image))
      return {StartupRelocationError::Metadata, i, 0};
    if (!object.metadata->owns_metadata()) {
      DynamicInfo checked;
      if (DynamicInfo::open(*object.image, checked) != DynamicError::None)
        return {StartupRelocationError::Metadata, i, 0};
    }
    RelocationDestinations destinations(*object.image, writable[i]);
    for (Address j = 0; j < object.metadata->relocation_count(); ++j) {
      DynamicRelocation r;
      Address resolved;
      if (!object.metadata->relocation(j, r))
        return {StartupRelocationError::Metadata, i, j};
      auto *target = destinations.get(r.offset);
      if (!target)
        return {StartupRelocationError::Destination, i, j};
      auto error = resolve(context, i, r, resolved);
      if (error != StartupRelocationError::None)
        return {error, i, j};
      if (!scratch.empty()) {
        if (relative_end == symbolic_begin)
          return {StartupRelocationError::Image, i, j};
        // Build both phases in one pass. Symbolic writes grow backwards,
        // so each phase can replay the original discovery order.
        size_t slot = is_relative_relocation(r.info) ? relative_end++
                                                      : --symbolic_begin;
        scratch[slot] = {target, resolved};
      }
    }
  }
  if (!scratch.empty()) {
    for (const auto &write : scratch.first(relative_end))
      write_relocation(write.destination, write.value);
    for (size_t i = scratch.size(); i > symbolic_begin;) {
      const auto &write = scratch[--i];
      write_relocation(write.destination, write.value);
    }
    return {StartupRelocationError::None, 0, 0};
  }
  // Metadata cannot overlap destinations, so resolving again is safe after
  // preflight. All objects' RELATIVE writes precede every symbolic write.
  for (unsigned phase = 0; phase < 2; ++phase) {
    for (size_t i = 0; i < objects.size(); ++i) {
      const auto &object = objects[i];
      RelocationDestinations destinations(*object.image, writable[i]);
      for (Address j = 0; j < object.metadata->relocation_count(); ++j) {
        DynamicRelocation r;
        (void)object.metadata->relocation(j, r);
        if (is_relative_relocation(r.info) != (phase == 0))
          continue;
        Address resolved;
        (void)resolve(context, i, r, resolved);
        write_relocation(destinations.get(r.offset), resolved);
      }
    }
  }
  return {StartupRelocationError::None, 0, 0};
}

static StartupRelocationResult
relocate(cpp::span<const SymbolObject> objects,
         cpp::span<const cpp::span<const WritableRegion>> writable,
         const StartupTLSSet *tls, cpp::span<RelocationWrite> scratch = {}) {
  StartupSymbols symbols;
  if (StartupSymbols::open(objects, symbols, tls != nullptr) !=
      SymbolError::None)
    return {StartupRelocationError::Image, 0, 0};
  RelocationSymbols cached(symbols);
  struct Context {
    cpp::span<const SymbolObject> objects;
    RelocationSymbols &symbols;
    const StartupTLSSet *tls;
  } context{objects, cached, tls};
  return relocate_objects(
      objects, writable,
      [](void *opaque, size_t i, const DynamicRelocation &r, Address &out) {
        auto &c = *static_cast<Context *>(opaque);
        return value(c.objects[i], c.symbols, i, r, out, c.tls);
      },
      &context, scratch);
}

StartupRelocationResult
relocate_startup(cpp::span<const SymbolObject> objects,
                 cpp::span<const cpp::span<const WritableRegion>> writable,
                 cpp::span<RelocationWrite> scratch) {
  return relocate(objects, writable, nullptr, scratch);
}

StartupRelocationResult
relocate_startup_tls(cpp::span<const SymbolObject> objects,
                     cpp::span<const cpp::span<const WritableRegion>> writable,
                     const StartupTLSSet &layout, RelocatedTLS &out,
                     cpp::span<RelocationWrite> scratch) {
  out.ready = nullptr;
  if (objects.empty() || layout.generation != 1 || !layout.modules ||
      layout.module_count != objects.size())
    return {StartupRelocationError::Image, 0, 0};
  Address cursor = startup_tls_header_size();
  Address alignment = startup_tls_min_alignment();
  for (size_t i = 0; i < objects.size(); ++i) {
    const auto *image = objects[i].image;
    if (!image || !image->is_mapped() ||
        (i == 0 ? image->role() != ImageRole::MainPIE &&
                      image->role() != ImageRole::MainExecutable
                : image->role() != ImageRole::SharedObject))
      return {StartupRelocationError::Image, i, 0};
    for (size_t j = 0; j < i; ++j)
      if (objects[j].image == image)
        return {StartupRelocationError::Image, i, 0};
    StartupTLSModule expected;
    if (!place_tls_module(*image, cursor, expected))
      return {StartupRelocationError::Value, i, 0};
    const auto &module = layout.modules[i];
    if (module.module_id != i + 1 || module.generation != 1 ||
        module.template_address != expected.template_address ||
        module.file_size != expected.file_size ||
        module.memory_size != expected.memory_size ||
        module.alignment != expected.alignment ||
        module.residue != expected.residue ||
        module.tp_offset != expected.tp_offset)
      return {StartupRelocationError::Image, i, 0};
    if (module.alignment > alignment)
      alignment = module.alignment;
  }
  if (layout.tp_alignment != alignment || layout.tls_extent != cursor ||
      alignment - 1 > ~Address(0) - cursor)
    return {StartupRelocationError::Value, 0, 0};
  auto result = relocate(objects, writable, &layout, scratch);
  if (result.error == StartupRelocationError::None)
    out.ready = &layout;
  return result;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
