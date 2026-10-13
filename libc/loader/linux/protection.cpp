//===-- Final loader image protections ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/protection.h"
#include "hdr/errno_macros.h"
#include "hdr/sys_mman_macros.h"
#include "loader/linux/syscall.h"
#include "loader/linux/target.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {

long finalize_image_protections(cpp::span<const ElfImage *const> images,
                                Address page_size) {
  if (images.empty() || !is_supported_page_size(page_size))
    return -EINVAL;
  for (const ElfImage *image : images)
    if (!image || !image->is_mapped() ||
        image->revalidate_layout(page_size) != ElfError::None)
      return -ENOEXEC;

  // Apply each page-disjoint LOAD independently, never spanning a hole. ELF
  // admission rejects W+X and executable GNU_STACK requests. The kernel owns
  // the initial stack mapping; this function does not guess its extent.
  for (unsigned phase = 0; phase < 2; ++phase) {
    for (const ElfImage *image : images) {
      for (unsigned i = 0; i < image->header()->e_phnum; ++i) {
        Elf64_Phdr p;
        (void)image->program_header(i, p);
        if (!p.p_memsz || p.p_type != (phase ? PT_GNU_RELRO : PT_LOAD))
          continue;
        // Admission has checked rounding, bias, coverage and page separation.
        Address start = p.p_vaddr & ~(page_size - 1);
        Address end =
            (p.p_vaddr + p.p_memsz + page_size - 1) & ~(page_size - 1);
        int prot = PROT_READ;
        if (!phase)
          prot |= ((p.p_flags & PF_W) ? PROT_WRITE : 0) |
                  ((p.p_flags & PF_X) ? PROT_EXEC : 0);
        long result = raw_syscall(SYS_mprotect, image->load_bias() + start,
                                  end - start, prot);
        if (result != 0)
          return result < 0 ? result : -EIO;
      }
    }
  }
  return 0;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
