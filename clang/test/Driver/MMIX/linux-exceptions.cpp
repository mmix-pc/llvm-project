// REQUIRES: mmix-registered-target
// RUN: %clangxx --target=mmix-unknown-linux -ffreestanding -### -c %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: %clangxx --target=mmix-unknown-linux -ffreestanding -c %s -o %t.o
// RUN: %clangxx --target=mmix-unknown-linux -ffreestanding -fdwarf-exceptions -c %s -o %t.explicit.o
// RUN: %clangxx --target=mmix-unknown-linux -ffreestanding -fno-exceptions -fexceptions -c %s -o %t.enabled.o
// RUN: %clangxx --target=mmix-unknown-linux -ffreestanding -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// RUN: not %clangxx --target=mmix-unknown-linux -ffreestanding -fexceptions -fno-exceptions -c %s -o %t.no.o 2>&1 | FileCheck %s --check-prefix=DISABLED
// RUN: not %clangxx --target=mmix-unknown-linux -ffreestanding -fsjlj-exceptions -c %s -o %t.sjlj.o 2>&1 | FileCheck %s --check-prefix=MODEL-ERROR
// RUN: %clang --target=mmix-unknown-linux -x c -ffreestanding -fexceptions -S -emit-llvm %s -o - | FileCheck %s --check-prefix=CLEANUP
// RUN: %clang --target=mmix-unknown-linux -x c -ffreestanding -fexceptions -c %s -o %t.c.o
// RUN: %clangxx --target=mmix-unknown-unknown -ffreestanding -c %s -o %t.generic.o
// MODEL: "-exception-model=dwarf"
// DISABLED: error: cannot use 'throw' with exceptions disabled
// MODEL-ERROR: MMIX does not support C++ exceptions
// IR: personality ptr @__gxx_personality_v0
// IR: invoke
// IR: landingpad
// CLEANUP: personality ptr @__gcc_personality_v0
// CLEANUP: invoke
// CLEANUP: landingpad
// CLEANUP: invoke void @cleanup
// CLEANUP: resume

#ifdef __cplusplus
struct Guard { ~Guard(); };
extern void call();
void raise() {
  Guard guard;
  call();
  throw 42;
}
#else
extern void cleanup(int *);
extern void call(void);
void with_cleanup(void) {
  int value __attribute__((cleanup(cleanup))) = 0;
  call();
}
#endif
