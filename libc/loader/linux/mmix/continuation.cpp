//===-- MMIX post-relocation startup
//---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/startup.h"
#include "src/__support/CPP/new.h"

using namespace LIBC_NAMESPACE::loader;

// BSS storage needs neither dynamic initialization nor a relocated pointer.
// Keep main metadata, graph and initializer completion state alive at handoff.
alignas(Startup) static unsigned char state[sizeof(Startup)];
static bool entered;
static Startup *startup;

extern "C" __attribute__((visibility("hidden"))) Address
__llvm_libc_loader_after_relocation(const InitialStack *stack,
                                    const ElfImage *self, Arena *bootstrap) {
  if (entered || !stack || !self || !bootstrap)
    fatal_startup();
  entered = true;
  startup = new (state) Startup(stack->value(AuxField::Pagesz));
  // The self view is no longer needed after preparation. Finish all fallible
  // temporary releases before invoking any newly loaded code.
  if (startup->prepare(*stack, *self).error || bootstrap->reset() ||
      startup->prepare_runtime()) {
    startup->reset();
    bootstrap->reset();
    fatal_startup();
  }
  return startup->entry();
}

extern "C" __attribute__((visibility("hidden"))) Address
__llvm_libc_loader_main_tp() {
  return startup->thread_pointer();
}

// Called in a new activation after the assembly root has installed TP.
extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_loader_after_tls() {
  if ((startup->thread_pointer() && startup->adopt_runtime()) || startup->run())
    fatal_startup();
}
