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

static unsigned char *destination(const ElfImage &image,
                                  cpp::span<const WritableRegion> writable,
                                  Address offset) {
  if (offset % 8)
    return nullptr;
  Bytes expected = image.mapped_memory_bytes(offset, 8, PF_R | PF_W);
  if (expected.size() != 8)
    return nullptr;
  for (const auto &region : writable) {
    if (!region.bytes.data() || offset < region.vaddr ||
        offset - region.vaddr > region.bytes.size() ||
        region.bytes.size() - (offset - region.vaddr) < 8)
      continue;
    unsigned char *p = region.bytes.data() + (offset - region.vaddr);
    if (p == expected.data())
      return p;
  }
  return nullptr;
}

static StartupRelocationError value(const SymbolObject &object,
                                    const StartupSymbols &symbols, size_t index,
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

StartupRelocationResult
relocate_objects(cpp::span<const SymbolObject> objects,
                 cpp::span<const cpp::span<const WritableRegion>> writable,
                 RelocationValue resolve, void *context) {
  if (objects.size() != writable.size() || !resolve)
    return {StartupRelocationError::Image, 0, 0};
  for (size_t i = 0; i < objects.size(); ++i) {
    const auto &object = objects[i];
    if (!object.image || !object.metadata || !object.image->is_mapped() ||
        object.image->role() == ImageRole::Interpreter)
      return {StartupRelocationError::Image, i, 0};
    // Revalidate before writing, including RELACOUNT, symbol indexes,
    // metadata exclusion, nonoverlap and unsupported relocation kinds.
    DynamicInfo checked;
    if (DynamicInfo::open(*object.image, checked) != DynamicError::None)
      return {StartupRelocationError::Metadata, i, 0};
    for (Address j = 0; j < object.metadata->relocation_count(); ++j) {
      DynamicRelocation r;
      Address resolved;
      if (!object.metadata->relocation(j, r))
        return {StartupRelocationError::Metadata, i, j};
      if (!destination(*object.image, writable[i], r.offset))
        return {StartupRelocationError::Destination, i, j};
      auto error = resolve(context, i, r, resolved);
      if (error != StartupRelocationError::None)
        return {error, i, j};
    }
  }
  // Metadata cannot overlap destinations, so resolving again is safe after
  // preflight. All objects' RELATIVE writes precede every symbolic write.
  for (unsigned phase = 0; phase < 2; ++phase) {
    for (size_t i = 0; i < objects.size(); ++i) {
      const auto &object = objects[i];
      for (Address j = 0; j < object.metadata->relocation_count(); ++j) {
        DynamicRelocation r;
        (void)object.metadata->relocation(j, r);
        if (is_relative_relocation(r.info) != (phase == 0))
          continue;
        Address resolved;
        (void)resolve(context, i, r, resolved);
        unsigned char *p = destination(*object.image, writable[i], r.offset);
        for (unsigned k = 0; k < 8; ++k) {
          p[7 - k] = static_cast<unsigned char>(resolved);
          resolved >>= 8;
        }
      }
    }
  }
  return {StartupRelocationError::None, 0, 0};
}

static StartupRelocationResult
relocate(cpp::span<const SymbolObject> objects,
         cpp::span<const cpp::span<const WritableRegion>> writable,
         const StartupTLSSet *tls) {
  StartupSymbols symbols;
  if (StartupSymbols::open(objects, symbols, tls != nullptr) !=
      SymbolError::None)
    return {StartupRelocationError::Image, 0, 0};
  struct Context {
    cpp::span<const SymbolObject> objects;
    const StartupSymbols &symbols;
    const StartupTLSSet *tls;
  } context{objects, symbols, tls};
  return relocate_objects(
      objects, writable,
      [](void *opaque, size_t i, const DynamicRelocation &r, Address &out) {
        auto &c = *static_cast<Context *>(opaque);
        return value(c.objects[i], c.symbols, i, r, out, c.tls);
      },
      &context);
}

StartupRelocationResult
relocate_startup(cpp::span<const SymbolObject> objects,
                 cpp::span<const cpp::span<const WritableRegion>> writable) {
  return relocate(objects, writable, nullptr);
}

StartupRelocationResult
relocate_startup_tls(cpp::span<const SymbolObject> objects,
                     cpp::span<const cpp::span<const WritableRegion>> writable,
                     const StartupTLSSet &layout, RelocatedTLS &out) {
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
  auto result = relocate(objects, writable, &layout);
  if (result.error == StartupRelocationError::None)
    out.ready = &layout;
  return result;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
