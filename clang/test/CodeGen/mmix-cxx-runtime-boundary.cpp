// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O0 \
// RUN:   -mrelocation-model static -fno-rtti \
// RUN:   -emit-obj -o %t.o %s
// RUN: llvm-readobj --sections --section-groups --symbols --relocations %t.o \
// RUN:   | FileCheck %s --check-prefix=OBJECT \
// RUN:   --implicit-check-not=.init_array --implicit-check-not=.eh_frame \
// RUN:   --implicit-check-not=.gcc_except_table --implicit-check-not=.tdata \
// RUN:   --implicit-check-not=.tbss --implicit-check-not=.dynamic
// RUN: llvm-nm --undefined-only %t.o \
// RUN:   | FileCheck %s --check-prefix=UNDEFINED \
// RUN:   --implicit-check-not=__cxa --implicit-check-not=__gxx_personality \
// RUN:   --implicit-check-not=_ZTI --implicit-check-not=_ZTS \
// RUN:   --implicit-check-not=_ZTV --implicit-check-not=_Znwm \
// RUN:   --implicit-check-not=_ZdlPv
// RUN: llvm-nm --undefined-only %t.o | count 1
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c++17 -O2 \
// RUN:   -mrelocation-model static -fno-rtti \
// RUN:   -emit-obj -o %t-opt.o %s
// RUN: llvm-readobj --sections %t-opt.o \
// RUN:   | FileCheck %s --check-prefix=OPT-SECTIONS \
// RUN:   --implicit-check-not=.init_array --implicit-check-not=.eh_frame \
// RUN:   --implicit-check-not=.gcc_except_table --implicit-check-not=.tdata \
// RUN:   --implicit-check-not=.tbss --implicit-check-not=.dynamic
// RUN: llvm-nm --undefined-only %t-opt.o \
// RUN:   | FileCheck %s --check-prefix=UNDEFINED
// RUN: llvm-nm --undefined-only %t-opt.o | count 1

extern "C" long source_owned_hook(long);

namespace producer {

struct ConstantState {
  long Bias;
};

constexpr ConstantState State{7};

template <typename T>
__attribute__((noinline)) T adjust(T Value) {
  return Value + State.Bias;
}

} // namespace producer

extern "C" long producer_entry(long Value) {
  return producer::adjust(Value) + source_owned_hook(Value);
}

// OBJECT:      Name: .group
// OBJECT:      Type: SHT_GROUP
// OBJECT:      Name: .text._ZN8producer6adjustIlEET_S1_
// OBJECT:      SHF_GROUP
// OBJECT:      R_MMIX_GETA _ZN8producer6adjustIlEET_S1_
// OBJECT:      R_MMIX_GETA source_owned_hook
// OBJECT:      Name: _ZN8producer6adjustIlEET_S1_
// OBJECT:      Binding: Weak
// OBJECT:      Type: COMDAT
// OBJECT-NEXT: Signature: _ZN8producer6adjustIlEET_S1_

// UNDEFINED: U source_owned_hook

// OPT-SECTIONS: Name: .text
