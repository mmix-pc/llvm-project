// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c17 -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/provider.o %t/provider.c
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/consumer.o %t/consumer.cpp
// RUN: llvm-readobj --symbols --relocations --section-groups %t/consumer.o \
// RUN:   | FileCheck %s --check-prefix=OBJECT
// RUN: ld.lld -m elf64mmix -e vector_entry -o %t/linked \
// RUN:   %t/provider.o %t/consumer.o
// RUN: llvm-nm --undefined-only %t/linked | count 0
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c17 -O2 \
// RUN:   -mrelocation-model static -emit-obj -o %t/provider-opt.o %t/provider.c
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O2 \
// RUN:   -mrelocation-model static -emit-obj -o %t/consumer-opt.o %t/consumer.cpp
// RUN: ld.lld -m elf64mmix -e vector_entry -o %t/linked-opt \
// RUN:   %t/provider-opt.o %t/consumer-opt.o
// RUN: llvm-nm --undefined-only %t/linked-opt | count 0

//--- vector.h
#if defined(__cplusplus)
extern "C" {
#endif

typedef unsigned u32x2 __attribute__((ext_vector_type(2)));
u32x2 c_transform(u32x2 value);

#if defined(__cplusplus)
}
#endif

//--- provider.c
#include "vector.h"

u32x2 c_transform(u32x2 value) { return value + (u32x2){1, 2}; }

//--- consumer.cpp
#include "vector.h"

struct Box {
  u32x2 Value;

  unsigned lane(unsigned Index) const { return Value[Index]; }
};

template <typename T> __attribute__((noinline)) T combine(T Lhs, T Rhs) {
  return Lhs + Rhs;
}

template u32x2 combine(u32x2, u32x2);

u32x2 Global = {3, 4};
__attribute__((weak)) u32x2 WeakGlobal = {5, 6};

extern "C" u32x2 vector_entry(Box *Object, u32x2 Input) {
  u32x2 &Reference = Object->Value;
  u32x2 Transformed = c_transform(Input);
  u32x2 Result = combine(Transformed, Reference);
  Result[0] += Object->lane(1);
  return Result + Global + WeakGlobal;
}

// The explicit vector template instantiation is weak and belongs to a COMDAT
// group; ordinary calls retain MMIX call relocations.
// OBJECT-DAG:  Name: _Z7combineIDv2_jET_S1_S1_
// OBJECT-DAG:  Binding: Weak
// OBJECT-DAG:  Type: COMDAT
// OBJECT-DAG:  R_MMIX_GETA c_transform
// OBJECT-DAG:  R_MMIX_GETA _Z7combineIDv2_jET_S1_S1_
// OBJECT-DAG:  Name: Global
// OBJECT-DAG:  Name: WeakGlobal
