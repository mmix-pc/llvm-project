// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/first.o %t/first.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/second.o %t/second.cpp
// RUN: llvm-readobj --sections --section-groups --symbols --relocations \
// RUN:   %t/first.o | FileCheck %s --check-prefix=OBJECT
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/linked %t/first.o %t/second.o
// RUN: llvm-readobj --sections --section-groups --symbols --relocations \
// RUN:   %t/linked | FileCheck %s --check-prefix=LINKED
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O2 \
// RUN:   -mrelocation-model static -emit-obj -o %t/first-opt.o %t/first.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O2 \
// RUN:   -mrelocation-model static -emit-obj -o %t/second-opt.o %t/second.cpp
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/linked-opt \
// RUN:   %t/first-opt.o %t/second-opt.o
// RUN: llvm-nm --undefined-only %t/linked-opt | count 0

//--- definitions.h
namespace object_model {

inline __attribute__((noinline)) long inline_value(long Value) {
  return Value + 1;
}

template <typename T>
__attribute__((noinline)) T template_value(T Value) {
  return Value + 2;
}

__attribute__((visibility("hidden"))) long hidden_value(long Value);
__attribute__((visibility("protected"))) long protected_value(long Value);
__attribute__((weak)) long weak_value(long Value);

} // namespace object_model

//--- first.cpp
#include "definitions.h"

namespace object_model {

long hidden_value(long Value) { return Value + 3; }
long protected_value(long Value) { return Value + 4; }
long weak_value(long Value) { return Value + 5; }

long first(long Value) {
  return inline_value(Value) + template_value(Value) + hidden_value(Value) +
         protected_value(Value) + weak_value(Value);
}

} // namespace object_model

extern "C" long second(long);
extern "C" long c_entry(long Value) {
  return object_model::first(Value) + second(Value);
}

//--- second.cpp
#include "definitions.h"

extern "C" long second(long Value) {
  return object_model::inline_value(Value) +
         object_model::template_value(Value);
}

// OBJECT:      Name: .group
// OBJECT:      Type: SHT_GROUP
// OBJECT:      Name: .text._ZN12object_model12inline_valueEl
// OBJECT:      SHF_GROUP
// OBJECT:      Name: .text._ZN12object_model14template_valueIlEET_S1_
// OBJECT:      SHF_GROUP
// OBJECT:      R_MMIX_GETA _ZN12object_model12inline_valueEl
// OBJECT:      R_MMIX_GETA _ZN12object_model14template_valueIlEET_S1_
// OBJECT:      Name: _ZN12object_model12hidden_valueEl
// OBJECT:      Other [ (0x2)
// OBJECT:      STV_HIDDEN
// OBJECT:      Name: _ZN12object_model15protected_valueEl
// OBJECT:      Other [ (0x3)
// OBJECT:      STV_PROTECTED
// OBJECT:      Name: _ZN12object_model10weak_valueEl
// OBJECT:      Binding: Weak
// OBJECT:      Name: _ZN12object_model12inline_valueEl
// OBJECT:      Binding: Weak
// OBJECT:      Name: _ZN12object_model14template_valueIlEET_S1_
// OBJECT:      Binding: Weak
// OBJECT-NOT:  .init_array
// OBJECT-NOT:  .dynamic
// OBJECT:      Type: COMDAT
// OBJECT-NEXT: Signature: _ZN12object_model12inline_valueEl
// OBJECT:      Type: COMDAT
// OBJECT-NEXT: Signature: _ZN12object_model14template_valueIlEET_S1_

// LINKED:      Name: .text
// LINKED-NOT:  Name: .MMIX.reg_contents
// LINKED:      Relocations [
// LINKED-NEXT: ]
// LINKED:      Name: _ZN12object_model12hidden_valueEl
// LINKED:      STV_HIDDEN
// LINKED:      Name: _ZN12object_model15protected_valueEl
// LINKED:      STV_PROTECTED
// LINKED:      Name: _ZN12object_model10weak_valueEl
// LINKED:      Binding: Weak
// LINKED:      Name: _ZN12object_model12inline_valueEl
// LINKED:      Binding: Weak
// LINKED:      Name: _ZN12object_model14template_valueIlEET_S1_
// LINKED:      Binding: Weak
// LINKED:      There are no group sections in the file.
// LINKED-NOT:  .init_array
// LINKED-NOT:  .dynamic
