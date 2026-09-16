// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-unknown -fsyntax-only %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -fsyntax-only -DMINIMUM=2048 -DDEFAULT=8192 %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux -fsyntax-only -DMINIMUM=5120 -DDEFAULT=16384 %s
// RUN: %clang_cc1 -triple riscv64-unknown-linux -fsyntax-only -DMINIMUM=2048 -DDEFAULT=8192 %s

#include "../../../libc/include/llvm-libc-macros/signal-macros.h"
#ifdef __linux__
_Static_assert(MINSIGSTKSZ == MINIMUM, "minimum unchanged");
_Static_assert(SIGSTKSZ == DEFAULT, "default unchanged");
#elif defined(MINSIGSTKSZ) || defined(SIGSTKSZ) || defined(SS_AUTODISARM)
#error "Linux signal stack policy leaked into Generic MMIX"
#endif
