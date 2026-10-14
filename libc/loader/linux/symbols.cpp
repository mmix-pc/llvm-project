//===-- Startup symbol binding --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/symbols.h"
#include "loader/linux/target.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

SymbolError StartupSymbols::open(cpp::span<const SymbolObject> views,
                                 StartupSymbols &out, bool allow_tls) {
  out.objects = {};
  if (views.empty())
    return SymbolError::Scope;
  for (const auto &view : views)
    if (!view.image || !view.image->header() || !view.metadata ||
        !view.metadata->symbol_count() ||
        (!allow_tls && view.metadata->requires_tls()))
      // Ordinary startup must retain this gate until TP adoption is wired in.
      return SymbolError::Scope;
  out.objects = views;
  return SymbolError::None;
}

SymbolError StartupSymbols::definition(size_t object, Address index,
                                       SymbolValue &out) const {
  Elf64_Sym symbol;
  if (!objects[object].metadata->symbol(index, symbol) ||
      symbol.st_shndx == SHN_UNDEF)
    return SymbolError::Index;
  Address address = symbol.st_value;
  if ((symbol.st_info & 15) == STT_TLS) {
    out = {address, object, index, false, true};
    return SymbolError::None;
  }
  if (symbol.st_shndx != SHN_ABS) {
    bool function = (symbol.st_info & 15) == STT_FUNC;
    Address size = symbol.st_size ? symbol.st_size : function ? 1 : 0;
    if (!objects[object].image->runtime_address(
            symbol.st_value, size, function ? PF_R | PF_X : PF_R, address) ||
        (function && !is_aligned_entry(address)))
      return SymbolError::Address;
  }
  out = {address, object, index, false};
  return SymbolError::None;
}

SymbolError StartupSymbols::lookup(Bytes name, SymbolValue &out) const {
  out = {};
  if (objects.empty())
    return SymbolError::Scope;
  for (size_t i = 0; i < objects.size(); ++i) {
    Address index;
    if (objects[i].metadata->find_definition(name, index))
      return definition(i, index, out);
  }
  return SymbolError::Undefined;
}

SymbolError StartupSymbols::resolve(size_t requester, Address index,
                                    SymbolValue &out) const {
  out = {};
  if (objects.empty())
    return SymbolError::Scope;
  if (requester >= objects.size() || !index)
    return SymbolError::Index;
  const auto &object = objects[requester];
  Elf64_Sym symbol;
  Bytes name;
  if (!object.metadata->symbol(index, symbol) ||
      !object.metadata->string(symbol.st_name, name))
    return SymbolError::Index;
  unsigned binding = symbol.st_info >> 4;
  bool internal = binding == STB_LOCAL || symbol.st_other != STV_DEFAULT;
  if (symbol.st_shndx != SHN_UNDEF && internal)
    return definition(requester, index, out);
  SymbolError error = internal ? SymbolError::Undefined : lookup(name, out);
  if (error == SymbolError::None &&
      out.tls != ((symbol.st_info & 15) == STT_TLS))
    return SymbolError::Type;
  if (error == SymbolError::Undefined && binding == STB_WEAK &&
      symbol.st_shndx == SHN_UNDEF && (symbol.st_info & 15) != STT_TLS) {
    out = {0, requester, index, true};
    return SymbolError::None;
  }
  return error;
}

SymbolError StartupSymbols::validate_references(SymbolFailure &failure) const {
  failure = {};
  if (objects.empty())
    return SymbolError::Scope;
  for (size_t i = 0; i < objects.size(); ++i) {
    const auto &metadata = *objects[i].metadata;
    for (Address j = 0; j < metadata.relocation_count(); ++j) {
      DynamicRelocation relocation;
      if (!metadata.relocation(j, relocation))
        return SymbolError::Index;
      Address index = relocation.info >> 32;
      if (!index)
        continue;
      SymbolValue value;
      SymbolError error = resolve(i, index, value);
      if (error != SymbolError::None) {
        failure = {i, index};
        return error;
      }
    }
  }
  return SymbolError::None;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
