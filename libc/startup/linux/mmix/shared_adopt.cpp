//===-- Post-TP shared libc adoption --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "libunwind/src/mmix/StartupImages.h"
#include "shared_startup.h"
#include "src/__support/OSUtil/linux/auxv.h"
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#include "src/__support/threads/linux/mmix/loader_cancellation.h"
#include "src/dlfcn/linux/mmix/loading.h"
#include "src/errno/program_invocation_name.h"
#include "src/errno/program_invocation_short_name.h"
#include "src/stdlib/atexit.h"
#include "src/unistd/environ.h"
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#endif

extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_linux_init_guard(const unsigned char *);

namespace LIBC_NAMESPACE_DECL {
AppProperties app;
namespace mmix {
static loader::LoaderServices services{};
static bool attempted;

long retain_shared_tls_destructor(void *dso, uintptr_t callback,
                                  uint64_t *token) {
  if (!services.images.retain)
    return EINVAL;
  return services.images.retain(services.images.context,
                                reinterpret_cast<uintptr_t>(dso), callback,
                                loader_cancellation_hooks(), token);
}

long retain_shared_process_callback(void *dso, uintptr_t callback,
                                    uint64_t *token) {
  if (!services.images.retain_process)
    return EINVAL;
  return services.images.retain_process(services.images.context,
                                        reinterpret_cast<uintptr_t>(dso),
                                        callback, loader_cancellation_hooks(),
                                        token);
}

long retain_shared_signal(uintptr_t handler, uintptr_t restorer,
                           loader::LoaderSignalLease *lease) {
  if (!services.signals.retain)
    return EINVAL;
  return services.signals.retain(services.signals.context, handler, restorer,
                                 lease);
}
void finish_shared_signal(loader::LoaderSignalLease *lease, bool installed) {
  services.signals.finish(services.signals.context, lease, installed);
}

long release_shared_tls_destructor(uint64_t token) {
  if (!services.images.release)
    return EINVAL;
  return services.images.release(services.images.context, token,
                                 loader_cancellation_hooks());
}

static void finalize_shared_startup() {
  services.finalize_startup(services.context, loader_cancellation_hooks());
}

long prepare_shared_thread_tls(void *state,
                               loader::ThreadTLSAllocation &allocation) {
  if (!services.prepare_thread_tls)
    return EINVAL;
  return services.prepare_thread_tls(services.context, state, &allocation);
}

long release_shared_thread_tls(loader::ThreadTLSAllocation &allocation) {
  if (!allocation.tls_mapping_size && !allocation.dtv_mapping_size &&
      !allocation.runtime_owner)
    return 0;
  if (!services.release_thread_tls)
    return EINVAL;
  return services.release_thread_tls(services.context, &allocation);
}

static long describe_image(long (*query)(void *, uintptr_t,
                                         loader::UnwindImageView *),
                           uintptr_t key, MMIXUnwindImage *out) {
  if (!out)
    return EINVAL;
  loader::UnwindImageView view{};
  *out = {};
  long error = query(services.context, key, &view);
  if (!error)
    *out = {view.load_bias,
            view.phdr_address,
            view.phdr_count,
            view.eh_frame_header,
            view.eh_frame_header_size,
            view.readable_ranges,
            view.range_count,
            view.token};
  return error;
}
static long find_image(void *, uintptr_t pc, MMIXUnwindImage *out) {
  return describe_image(services.find_unwind_image, pc, out);
}
static long acquire_image(void *, uintptr_t pc, MMIXUnwindImage *out) {
  return describe_image(services.acquire_unwind_image, pc, out);
}
static long retain_image(void *, uintptr_t token, MMIXUnwindImage *out) {
  return describe_image(services.retain_unwind_image, token, out);
}
static long drop_image(void *, uintptr_t token) {
  return services.drop_unwind_image(services.context, token);
}
static void release_image(void *, uintptr_t token) {
  services.release_unwind_image(services.context, token);
}

long adopt_shared_main(const loader::StartupProcessView *process,
                       const loader::LoaderServices *input, void *state,
                       const loader::ThreadTLSAllocation *tls) {
  if (attempted || !process || !input || state != &shared_main || !tls ||
      input->size != sizeof(services) || !input->context || !input->tls_set ||
      !input->prepare_thread_tls || !input->release_thread_tls ||
      !input->resolve_startup_tls || !input->find_unwind_image ||
      !input->release_unwind_image || !input->acquire_unwind_image ||
      !input->retain_unwind_image || !input->drop_unwind_image ||
      !input->finalize_startup ||
      !input->run_initializers || !input->images.context ||
      !input->images.retain || !input->images.release ||
      !input->images.retain_process || !input->signals.context ||
      !input->signals.retain || !input->signals.finish || !tls->tp ||
      reinterpret_cast<uintptr_t>(__builtin_thread_pointer()) != tls->tp ||
      reinterpret_cast<const uintptr_t *>(tls->tp)[1] !=
          reinterpret_cast<uintptr_t>(state))
    return EINVAL;
  attempted = true;
  __builtin_memcpy_inline(&services, input, sizeof(services));
  if (!adopt_startup_tls_resolver(
          {services.context, services.resolve_startup_tls}))
    return EINVAL;
  __llvm_libc_mmix_linux_init_guard(
      reinterpret_cast<const unsigned char *>(process->random));
  app.args = reinterpret_cast<Args *>(process->original_sp);
  app.env_ptr = reinterpret_cast<uintptr_t *>(process->envp);
  app.page_size = process->page_size;
  // A dynamic module set is not a static executable TLS template.
  app.tls = {};
  auxv::Vector::initialize_unsafe(
      reinterpret_cast<const auxv::Entry *>(process->auxv));
  environ = reinterpret_cast<char **>(process->envp);
  if (process->argc) {
    program_invocation_name = reinterpret_cast<char **>(process->argv)[0];
    program_invocation_short_name = program_invocation_name;
    for (char *p = program_invocation_name; *p; ++p)
      if (*p == '/')
        program_invocation_short_name = p + 1;
  }
  shared_main.tls = {tls->tls_mapping_size, tls->tls_mapping_base, tls->tp};
  shared_main.shared_tls = *tls;
  shared_main.attributes.tls = tls->tls_mapping_base;
  shared_main.attributes.tls_size = tls->tls_mapping_size;
  shared_main.attributes.platform_data = &shared_main.clear_tid.value.val;
  if (!internal::activate_main_thread(shared_main))
    return EINVAL;
  if (!adopt_loading_services(services.api) ||
      !adopt_query_services(services.queries) ||
      !adopt_loader_process_services(services.process))
    return EINVAL;
  MMIXStartupUnwindService unwind{sizeof(unwind), 1, &services, find_image,
                                  release_image, acquire_image, retain_image,
                                  drop_image};
  if (!__libunwind_mmix_adopt_startup_images(&unwind))
    return EINVAL;
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  initialize_cancellation_process();
#endif
  // Register before preinit/constructors: their callbacks run first through
  // the same process manager. _Exit bypasses this path entirely.
  if (atexit(finalize_shared_startup))
    return ENOMEM;
  return services.run_initializers(services.context,
                                   loader_cancellation_hooks());
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
