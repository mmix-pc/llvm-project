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
                                    const DynamicRelocation &r, Address &out) {
  Address base = object.image->load_bias();
  if (!is_relative_relocation(r.info)) {
    SymbolValue symbol;
    if (symbols.resolve(index, r.info >> 32, symbol) != SymbolError::None)
      return StartupRelocationError::Symbol;
    base = symbol.address;
  }
  return runtime_relocation_value(r.info, base, r.addend, out)
             ? StartupRelocationError::None
             : StartupRelocationError::Value;
}

StartupRelocationResult
relocate_startup(cpp::span<const SymbolObject> objects,
                 cpp::span<const cpp::span<const WritableRegion>> writable) {
  StartupSymbols symbols;
  if (objects.size() != writable.size() ||
      StartupSymbols::open(objects, symbols) != SymbolError::None)
    return {StartupRelocationError::Image, 0, 0};
  for (size_t i = 0; i < objects.size(); ++i) {
    const auto &object = objects[i];
    if (!object.image->is_mapped() ||
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
      auto error = value(object, symbols, i, r, resolved);
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
        (void)value(object, symbols, i, r, resolved);
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

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
