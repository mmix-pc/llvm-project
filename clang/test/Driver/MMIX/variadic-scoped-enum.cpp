// REQUIRES: mmix-registered-target
// RUN: %clangxx --target=mmix-unknown-linux -ffreestanding -std=c++17 -O0 -c %s -o %t.o
// RUN: %clangxx --target=mmix-unknown-linux -ffreestanding -std=c++17 -O2 -c %s -o %t.opt.o
// RUN: %clangxx --target=mmix-unknown-unknown -ffreestanding -std=c++17 -O0 -c %s -o %t.generic.o
// RUN: %clangxx --target=mmix-unknown-linux -ffreestanding -std=c++17 -O0 -S -emit-llvm %s -o - | FileCheck %s

enum class Byte : unsigned char { value = 255 };
enum class Short : short { value = -123 };
extern void sink(...);
void send(Byte b, Short s) {
  // CHECK: call void (...) {{.*}}(i8{{.*}} zeroext {{.*}}, i16{{.*}} signext
  sink(b, s);
  // Reach overflow-stack slots after sixteen argument registers.
  sink(b, s, b, s, b, s, b, s, b, s, b, s, b, s, b, s, b, s);
}
Byte fetch(int marker, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, marker);
  // The narrow enum stays right-adjusted in its octa-sized slot.
  // CHECK: getelementptr inbounds i8, ptr {{.*}}, i64 8
  // CHECK: getelementptr inbounds i8, ptr {{.*}}, i64 7
  // CHECK: load i8
  Byte b = __builtin_va_arg(ap, Byte);
  __builtin_va_end(ap);
  return b;
}
