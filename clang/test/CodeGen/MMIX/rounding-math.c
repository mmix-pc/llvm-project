// RUN: %clang_cc1 -triple mmix-unknown-linux -Werror -frounding-math -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -Werror -frounding-math -ffp-exception-behavior=strict -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix -mrelocation-model static -Werror -frounding-math -emit-obj -o %t.o %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -Werror -frounding-math -O2 -emit-obj -o %t.o %s

#pragma STDC FENV_ACCESS ON

// CHECK-LABEL: define{{.*}} @add
// CHECK: call double @llvm.experimental.constrained.fadd.f64
// CHECK-SAME: metadata !"round.dynamic"
double add(double x, double y) { return x + y; }

// CHECK-LABEL: define{{.*}} @round_to_float
// CHECK: call float @llvm.experimental.constrained.fptrunc.f32.f64
// CHECK-SAME: metadata !"round.dynamic"
float round_to_float(double x) { return x; }

// CHECK-LABEL: define{{.*}} @from_integer
// CHECK: call float @llvm.experimental.constrained.sitofp.f32.i64
// CHECK-SAME: metadata !"round.dynamic"
float from_integer(long x) { return x; }

// CHECK-LABEL: define{{.*}} @less
// CHECK: call i1 @llvm.experimental.constrained.fcmps.f64
int less(double x, double y) { return x < y; }

// CHECK-LABEL: define{{.*}} @equal
// CHECK: call i1 @llvm.experimental.constrained.fcmp.f64
int equal(double x, double y) { return x == y; }

// Exercise the strict libcall selection without requiring a runtime link.
#define UNARY(name) \
  double call_##name(double x) { return __builtin_##name(x); } \
  float call_##name##f(float x) { return __builtin_##name##f(x); }
UNARY(acos)
UNARY(asin)
UNARY(atan)
UNARY(cos)
UNARY(cosh)
UNARY(exp)
UNARY(exp2)
UNARY(log)
UNARY(log10)
UNARY(log2)
UNARY(sin)
UNARY(sinh)
UNARY(tan)
UNARY(tanh)
UNARY(ceil)
UNARY(floor)
UNARY(trunc)
UNARY(round)
UNARY(nearbyint)
UNARY(rint)
UNARY(sqrt)
long call_lrint(double x) { return __builtin_lrint(x); }
long call_lround(float x) { return __builtin_lroundf(x); }
long long call_llrint(float x) { return __builtin_llrintf(x); }
long long call_llround(double x) { return __builtin_llround(x); }
double call_pow(double x, double y) { return __builtin_pow(x, y); }
float call_atan2(float x, float y) { return __builtin_atan2f(x, y); }
double call_min(double x, double y) { return __builtin_fmin(x, y); }
float call_max(float x, float y) { return __builtin_fmaxf(x, y); }
double call_scale(double x, int n) { return __builtin_ldexp(x, n); }
