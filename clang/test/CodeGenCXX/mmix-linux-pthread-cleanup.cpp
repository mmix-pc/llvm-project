// REQUIRES: mmix-registered-target
// RUN: %clang --target=mmix-unknown-linux -std=c++17 -fexceptions -O2 -S -emit-llvm %s -o - | FileCheck %s --implicit-check-not=__llvm_libc_mmix_thread_cleanup_
// RUN: not %clang --target=mmix-unknown-linux -std=c++17 -fsyntax-only -DUNPAIRED %s 2>&1 | FileCheck %s --check-prefix=BAD

#include "../../../libc/include/llvm-libc-macros/mmix/pthread-cleanup.h"
extern void callback(void *);
extern void action();
void owner(void *p) {
#ifdef UNPAIRED
  pthread_cleanup_pop(1);
// BAD: error: use of undeclared identifier '__mmix_cleanup'
#else
  pthread_cleanup_push(callback, p);
  action();
  pthread_cleanup_pop(1);
#endif
}
// CHECK-LABEL: define {{.*}}void @_Z5ownerPv
// CHECK-SAME: #[[ATTR:[0-9]+]] personality
// CHECK: invoke void @_Z6actionv
// CHECK-NEXT: to label %{{.*}} unwind label %[[UNWIND:[a-zA-Z0-9._]+]]
// CHECK: store i8 0, ptr
// CHECK: invoke void {{(@_Z8callbackPv|%[a-zA-Z0-9._]+)}}(
// CHECK: [[UNWIND]]:
// CHECK: landingpad
// CHECK-NEXT: cleanup
// CHECK: store i8 0, ptr
// CHECK: invoke void {{(@_Z8callbackPv|%[a-zA-Z0-9._]+)}}(
// CHECK: resume
// CHECK: attributes #[[ATTR]] = { {{.*}}noinline{{.*}}uwtable{{.*}}"disable-tail-calls"="true"
