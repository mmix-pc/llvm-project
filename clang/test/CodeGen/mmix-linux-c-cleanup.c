// REQUIRES: mmix-registered-target
// RUN: %clang --target=mmix-unknown-linux -ffreestanding -nostdlibinc -I %S/../../../libc/src/__support/threads/linux/mmix -O2 -funwind-tables -fno-exceptions -S -emit-llvm %s -o - | FileCheck %s
// RUN: %clang --target=mmix-unknown-linux -ffreestanding -nostdlibinc -I %S/../../../libc/src/__support/threads/linux/mmix -O2 -funwind-tables -fno-exceptions -c %s -o %t.o
// RUN: llvm-readobj --sections %t.o | FileCheck %s --check-prefix=OBJ

#include "cleanup.h"
extern void callback(void *);
extern void leave(void) __attribute__((noreturn));

MMIX_CLEANUP_FRAME void owner(void) {
  struct MmixCleanupRecord record;
  int value = 7;
  __llvm_libc_mmix_thread_cleanup_push(&record, callback, &value);
  leave();
}

// The owning C frame remains unwindable without C language landing pads.
// CHECK: define {{.*}}void @owner() {{.*}}#[[ATTR:[0-9]+]]
// CHECK: call void @__llvm_libc_mmix_thread_cleanup_push
// CHECK: call void @leave()
// CHECK: attributes #[[ATTR]] = { {{.*}}noinline{{.*}}uwtable{{.*}}"disable-tail-calls"="true"
// OBJ: Name: .eh_frame
