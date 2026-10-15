//===-- Runtime TLS resolution --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_tls.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/sys_mman_macros.h"
#include "loader/linux/syscall.h"
#include "loader/linux/target.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {

[[noreturn]] static void fail() {
  static constexpr char message[] = "LLVM loader: dynamic TLS failure\n";
  size_t done = 0;
  for (unsigned i = 0; i < 8 && done < sizeof(message) - 1; ++i) {
    long result =
        raw_syscall(SYS_write, 2, reinterpret_cast<long>(message + done),
                    sizeof(message) - 1 - done);
    if (result == -EINTR)
      continue;
    if (result <= 0 || size_t(result) > sizeof(message) - 1 - done)
      break;
    done += result;
  }
  raw_syscall(SYS_exit_group, 127);
  __builtin_trap();
}

void *resolve_runtime_tls(void *context, const TLSIndex *index) {
  if (!context || !index ||
      reinterpret_cast<Address>(index) % alignof(TLSIndex))
    fail();
  const auto &ctx = *static_cast<const RuntimeTLSContext *>(context);
  if (!ctx.registry || !ctx.allocation || !ctx.refresh ||
      !is_supported_page_size(ctx.page_size) || !index->module_id)
    fail();
  const ImageRegistry::Image *image = nullptr;
  for (const auto *candidate : ctx.registry->images)
    if (candidate && candidate->tls.module_id == index->module_id) {
      image = candidate;
      break;
    }
  if (!image || !image->elf.is_mapped() || !image->elf.tls_header())
    fail();
  const auto &module = image->tls;
  Address cursor = 0;
  StartupTLSModule expected{};
  // Never read a template through an unchecked descriptor. Published records
  // must still describe the admitted PT_TLS and its readable mapped bytes.
  if (!module.generation || module.generation > ctx.registry->generation ||
      index->offset > module.memory_size ||
      !place_tls_module(image->elf, cursor, expected) ||
      module.template_address != expected.template_address ||
      module.file_size != expected.file_size ||
      module.memory_size != expected.memory_size ||
      module.alignment != expected.alignment ||
      module.residue != expected.residue)
    fail();

  uint64_t full = ~uint64_t(0), saved = 0;
  if (raw_syscall(SYS_rt_sigprocmask, SIG_BLOCK, reinterpret_cast<long>(&full),
                  reinterpret_cast<long>(&saved), sizeof(full)))
    fail();
  // refresh's nested mask restores our blocked mask, not the caller's mask.
  if (ctx.refresh->refresh(*ctx.registry, *ctx.allocation, ctx.page_size))
    fail();
  Address pointer = __atomic_load_n(
      reinterpret_cast<Address *>(ctx.allocation->tp), __ATOMIC_ACQUIRE);
  auto &dtv = *reinterpret_cast<StartupDTV *>(pointer);
  if (index->module_id >= dtv.capacity ||
      dtv.generation != ctx.registry->generation)
    fail();
  auto &entry =
      reinterpret_cast<StartupDTVEntry *>(dtv.entries)[index->module_id];
  if (!entry.generation) {
    if (image->startup || entry.image_base || entry.allocation_owner ||
        entry.allocation_extent)
      fail();
    // Reserve one addressable byte even for an empty module. Alignment is
    // relative to the ELF residue, not necessarily zero modulo p_align.
    Address extent = module.memory_size ? module.memory_size : 1;
    constexpr Address limit = __LONG_MAX__;
    if (module.alignment - 1 > limit - (ctx.page_size - 1) ||
        extent > limit - (ctx.page_size - 1) - (module.alignment - 1))
      fail();
    Address size = (extent + module.alignment - 1 + ctx.page_size - 1) &
                   ~(ctx.page_size - 1);
    long result = raw_syscall(SYS_mmap, 0, size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result < 0)
      fail();
    Address base = result;
    if (!base || size > ~Address(0) - base)
      fail();
    Address address = base + ((module.residue - base) & (module.alignment - 1));
    auto *destination = reinterpret_cast<volatile unsigned char *>(address);
    auto *source = reinterpret_cast<const volatile unsigned char *>(
        module.template_address);
    for (Address i = 0; i < module.file_size; ++i)
      destination[i] = source[i];
    // Anonymous mmap zeroes BSS and alignment padding; never touch the source.
    entry.image_base = address;
    entry.allocation_owner = base;
    entry.allocation_extent = size;
    __atomic_store_n(&entry.generation, module.generation, __ATOMIC_RELEASE);
  }
  if (entry.generation != module.generation || !entry.image_base ||
      index->offset > ~Address(0) - entry.image_base)
    fail();
  Address result = entry.image_base + index->offset;
  if (raw_syscall(SYS_rt_sigprocmask, SIG_SETMASK,
                  reinterpret_cast<long>(&saved), 0, sizeof(saved)))
    fail();
  return reinterpret_cast<void *>(result);
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
