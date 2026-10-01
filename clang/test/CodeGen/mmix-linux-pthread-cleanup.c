// REQUIRES: mmix-registered-target
// RUN: %clang --target=mmix-unknown-linux -ffreestanding -nostdlibinc -O2 -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions -S -emit-llvm %s -o - | FileCheck %s
// RUN: %clang --target=mmix-unknown-linux -ffreestanding -nostdlibinc -O2 -flto -fno-exceptions -c %s -o %t.bc
// RUN: llvm-dis %t.bc -o - | FileCheck %s
// RUN: %clang --target=mmix-unknown-linux -ffreestanding -nostdlibinc -O2 -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions -c %s -o %t.o
// RUN: llvm-readobj --sections %t.o | FileCheck %s --check-prefix=OBJ

#include "../../../libc/include/llvm-libc-macros/mmix/pthread-cleanup.h"

_Static_assert(sizeof(struct __llvm_libc_mmix_cleanup_record) == 96, "record ABI");
_Static_assert(_Alignof(struct __llvm_libc_mmix_cleanup_record) == 8, "record alignment");
extern void callback(void *);
extern void leave(void) __attribute__((noreturn));

// No caller attribute or unwind flag is required for the macro's C owner.
void owner(void) {
  int value = 7;
  pthread_cleanup_push(callback, &value);
  leave();
  pthread_cleanup_pop(0);
}

// CHECK: define {{.*}}void @owner() {{.*}}#[[ATTR:[0-9]+]]
// CHECK: call void @__llvm_libc_mmix_thread_cleanup_push
// CHECK: call void @leave()
// CHECK: attributes #[[ATTR]] = { {{.*}}noinline{{.*}}uwtable{{.*}}"disable-tail-calls"="true"
// OBJ: Name: .eh_frame
