//===-- Startup TLS layout planning ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/tls_layout.h"
#include "loader/linux/target.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static bool place(const ElfImage &image, Address &cursor,
                  StartupTLSModule &module) {
  module.template_address = module.file_size = module.memory_size = 0;
  module.alignment = 1;
  module.residue = 0;
  if (const auto *tls = image.tls_header()) {
    module.file_size = tls->p_filesz;
    module.memory_size = tls->p_memsz;
    module.alignment = tls->p_align ? tls->p_align : 1;
    module.residue = tls->p_vaddr & (module.alignment - 1);
    if (tls->p_filesz && !image.runtime_address(tls->p_vaddr, tls->p_filesz,
                                                PF_R, module.template_address))
      return false;
  }
  // Unsigned subtraction modulo a power of two gives the smallest padding.
  Address padding = (module.residue - cursor) & (module.alignment - 1);
  if (padding > ~Address(0) - cursor)
    return false;
  module.tp_offset = cursor + padding;
  if (module.memory_size > ~Address(0) - module.tp_offset)
    return false;
  cursor = module.tp_offset + module.memory_size;
  return true;
}

TLSLayoutError plan_startup_tls(cpp::span<const ElfImage *const> images,
                                cpp::span<StartupTLSModule> storage,
                                StartupTLSSet &out) {
  __builtin_memset_inline(&out, 0, sizeof(out));
  if (images.empty())
    return TLSLayoutError::Scope;
  if (storage.size() < images.size())
    return TLSLayoutError::Storage;
  Address cursor = startup_tls_header_size();
  Address alignment = startup_tls_min_alignment();
  // Preflight the entire scope before writing even the first descriptor.
  for (size_t i = 0; i < images.size(); ++i) {
    const auto *image = images[i];
    if (!image || !image->header() ||
        (i == 0 ? image->role() != ImageRole::MainExecutable &&
                      image->role() != ImageRole::MainPIE
                : image->role() != ImageRole::SharedObject))
      return TLSLayoutError::Scope;
    for (size_t j = 0; j < i; ++j)
      if (images[j] == image)
        return TLSLayoutError::Scope;
    StartupTLSModule module;
    if (!place(*image, cursor, module))
      return TLSLayoutError::Overflow;
    if (module.alignment > alignment)
      alignment = module.alignment;
  }
  // The allocation helper must also have room to align the eventual TP.
  if (alignment - 1 > ~Address(0) - cursor)
    return TLSLayoutError::Overflow;
  Address extent = cursor;
  cursor = startup_tls_header_size();
  for (size_t i = 0; i < images.size(); ++i) {
    StartupTLSModule &module = storage[i];
    (void)place(*images[i], cursor, module);
    module.module_id = i + 1;
    module.generation = 1;
  }
  out.generation = 1;
  out.module_count = images.size();
  out.modules = storage.data();
  out.tp_alignment = alignment;
  out.tls_extent = extent;
  return TLSLayoutError::None;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
