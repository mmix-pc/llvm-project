// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/first.o %t/first.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/second.o %t/second.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/entry.o %t/entry.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O0 \
// RUN:   -mrelocation-model static -emit-obj -o %t/runtime.o %t/runtime.cpp
// RUN: llvm-readobj --section-groups --sections --symbols --relocations \
// RUN:   %t/first.o | FileCheck %s --check-prefix=OBJECT
// RUN: llvm-readobj --relocations %t/first.o \
// RUN:   | FileCheck %s --check-prefix=RELOC
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/direct %t/entry.o %t/first.o \
// RUN:   %t/second.o %t/runtime.o
// RUN: llvm-nm --undefined-only %t/direct | count 0
// RUN: llvm-nm --defined-only %t/direct \
// RUN:   | FileCheck %s --check-prefix=LINKED
// RUN: llvm-ar rc %t/libowners.a %t/first.o %t/second.o
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/archive %t/entry.o \
// RUN:   --whole-archive %t/libowners.a --no-whole-archive %t/runtime.o
// RUN: llvm-nm --undefined-only %t/archive | count 0
// RUN: llvm-nm --defined-only %t/archive \
// RUN:   | FileCheck %s --check-prefix=LINKED
// RUN: ld.lld -m elf64mmix -r -o %t/partial.o %t/first.o %t/second.o
// RUN: ld.lld -m elf64mmix -r -o %t/repeated.o %t/partial.o %t/entry.o
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/partial-linked %t/repeated.o \
// RUN:   %t/runtime.o
// RUN: llvm-nm --undefined-only %t/partial-linked | count 0
// RUN: llvm-nm --defined-only %t/partial-linked \
// RUN:   | FileCheck %s --check-prefix=LINKED
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-obj -o %t/first-opt.o %t/first.cpp
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -fno-rtti -O2 \
// RUN:   -mrelocation-model static -emit-obj -o %t/second-opt.o %t/second.cpp
// RUN: ld.lld -m elf64mmix -e c_entry -o %t/optimized %t/entry.o \
// RUN:   %t/first-opt.o %t/second-opt.o %t/runtime.o
// RUN: llvm-nm --undefined-only %t/optimized | count 0
// RUN: llvm-nm --defined-only %t/optimized \
// RUN:   | FileCheck %s --check-prefix=OPT-LINKED

//--- ownership.h
extern long make_value(long);

inline long increment(long value) { return value + 1; }

template <class T> struct Holder {
  T value;
  explicit Holder(T input) : value(input) {}
  ~Holder() { value = 0; }
  T get() const { return value; }
};

struct Poly {
  long value;
  explicit Poly(long input) : value(input) {}
  virtual ~Poly() {}
  virtual long get() const { return value; }
};

inline Holder<long> shared(make_value(3));

long first(long);
long second(long);

//--- first.cpp
#include "ownership.h"

long first(long value) {
  Poly object(value);
  return increment(object.get()) + shared.get();
}

//--- second.cpp
#include "ownership.h"

long second(long value) {
  Poly object(value + 1);
  return increment(object.get()) + shared.get();
}

//--- entry.cpp
#include "ownership.h"

extern "C" long c_entry() { return first(1) + second(2); }

//--- runtime.cpp
extern "C" {
char __dso_handle = 0;
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
int __cxa_guard_acquire(void *) { return 1; }
void __cxa_guard_release(void *) {}
}

long make_value(long value) { return value; }
using size_t = decltype(sizeof(0));
void operator delete(void *, size_t) noexcept {}

// OBJECT:      Groups {
// OBJECT-DAG:  Signature: _Z9incrementl
// OBJECT-DAG:  Signature: _ZN4PolyC2El
// OBJECT-DAG:  Signature: _ZN6HolderIlEC2El
// OBJECT-DAG:  Signature: _ZTV4Poly
// OBJECT-DAG:  Signature: shared
// RELOC-DAG:   R_MMIX_64
// RELOC-DAG:   R_MMIX_GETA

// LINKED-COUNT-1: W _Z9incrementl
// LINKED-COUNT-1: V _ZGV6shared
// LINKED-COUNT-1: W _ZN4PolyC1El
// LINKED-COUNT-1: W _ZNK4Poly3getEv
// LINKED-COUNT-1: V _ZTV4Poly
// LINKED-COUNT-1: V shared

// OPT-LINKED-COUNT-1: V _ZGV6shared
// OPT-LINKED-COUNT-1: W _ZN6HolderIlEC1El
// OPT-LINKED-COUNT-1: V shared
