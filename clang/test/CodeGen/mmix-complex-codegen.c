// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c17 \
// RUN:   -mrelocation-model static -emit-llvm -disable-llvm-passes \
// RUN:   -o %t.ll %s
// RUN: FileCheck %s < %t.ll
// RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=obj \
// RUN:   %t.ll -o %t.o
// RUN: %clang --target=mmix-unknown-elf -ffreestanding -std=c17 -O1 \
// RUN:   -c -o %t.driver.o %s
// RUN: llvm-readobj --relocations --symbols %t.driver.o \
// RUN:   | FileCheck %s --check-prefix=ELF \
// RUN:     --implicit-check-not=__multc3 --implicit-check-not=__divtc3

typedef _Complex float complex_float;
typedef _Complex double complex_double;
typedef _Complex long double complex_long_double;

// ELF-DAG: R_MMIX_GETA __mulsc3
// ELF-DAG: R_MMIX_GETA __divsc3
// ELF-DAG: R_MMIX_GETA __muldc3
// ELF-DAG: R_MMIX_GETA __divdc3

struct ComplexMember {
  int tag;
  complex_double value;
};

complex_float global_float = __builtin_complex(1.0f, 2.0f);
struct ComplexMember global_member = {3, __builtin_complex(4.0, 5.0)};
complex_long_double global_array[2];
volatile complex_double volatile_global;

// CHECK: @global_float = dso_local global { float, float } { float 1.000000e+00, float 2.000000e+00 }, align 4
// CHECK: @global_member = dso_local global { i32, [4 x i8], { double, double } } { i32 3, [4 x i8] zeroinitializer, { double, double } { double 4.000000e+00, double 5.000000e+00 } }, align 8
// CHECK: @global_array = dso_local global [2 x { double, double }] zeroinitializer, align 8
// CHECK: @volatile_global = dso_local global { double, double } zeroinitializer, align 8

complex_float initialize_and_assign(float real, float imaginary) {
  complex_float initialized = __builtin_complex(real, imaginary);
  complex_float assigned = 0;
  assigned = initialized;
  return assigned;
}

// CHECK-LABEL: define dso_local i64 @initialize_and_assign(
// CHECK: store float %{{[^,]+}}, ptr %initialized.realp, align 4
// CHECK: store float %{{[^,]+}}, ptr %initialized.imagp, align 4
// CHECK: store float %initialized.real, ptr %assigned.realp3, align 4
// CHECK: store float %initialized.imag, ptr %assigned.imagp4, align 4
// CHECK: ret i64

double access_components(complex_double value) {
  return __real__ value + __imag__ value;
}

// CHECK-LABEL: define dso_local double @access_components(
// CHECK: [[COMPONENT_SUM:%[^ ]+]] = fadd double %{{[^,]+}}, %{{[^ ]+}}
// CHECK: ret double [[COMPONENT_SUM]]

complex_double conjugate_value(complex_double value) {
  return __builtin_conj(value);
}

// CHECK-LABEL: define dso_local { double, double } @conjugate_value(
// CHECK: [[NEGATED:%[^ ]+]] = fneg double %{{[^ ]+}}
// CHECK: store double [[NEGATED]], ptr %retval.imagp, align 8
// CHECK: ret { double, double }

complex_float arithmetic_float(complex_float lhs, complex_float rhs) {
  return (lhs + rhs) * (lhs - rhs) / rhs;
}

// CHECK-LABEL: define dso_local i64 @arithmetic_float(
// CHECK: fadd float
// CHECK: fsub float
// CHECK: fmul float
// CHECK: fcmp uno float
// CHECK: call i64 @__mulsc3(float
// CHECK: call i64 @__divsc3(float
// CHECK: ret i64

complex_double arithmetic_double(complex_double lhs, complex_double rhs) {
  return (lhs + rhs) * (lhs - rhs) / rhs;
}

// CHECK-LABEL: define dso_local { double, double } @arithmetic_double(
// CHECK: fadd double
// CHECK: fsub double
// CHECK: fmul double
// CHECK: fcmp uno double
// CHECK: call { double, double } @__muldc3(double
// CHECK: call { double, double } @__divdc3(double
// CHECK: ret { double, double }

complex_long_double arithmetic_long_double(complex_long_double lhs,
                                           complex_long_double rhs) {
  return (lhs + rhs) * (lhs - rhs) / rhs;
}

// CHECK-LABEL: define dso_local { double, double } @arithmetic_long_double(
// CHECK: fadd double
// CHECK: fsub double
// CHECK: call { double, double } @__muldc3(double
// CHECK: call { double, double } @__divdc3(double
// CHECK: ret { double, double }

int compare_values(complex_double lhs, complex_double rhs) {
  return lhs == rhs || lhs != rhs;
}

// CHECK-LABEL: define dso_local i32 @compare_values(
// CHECK: fcmp oeq double
// CHECK: fcmp une double
// CHECK: ret i32

complex_double cast_and_select(int condition, long value, complex_double lhs,
                               complex_double rhs) {
  complex_double converted = value;
  return condition ? lhs : (rhs + converted);
}

// CHECK-LABEL: define dso_local { double, double } @cast_and_select(
// CHECK: sitofp i64 %{{[^ ]+}} to double
// CHECK: store double 0.000000e+00, ptr %converted.imagp, align 8
// CHECK: %cond.r = phi double
// CHECK: %cond.i = phi double
// CHECK: ret { double, double }

double discard_imaginary(complex_double value) { return (double)value; }

// CHECK-LABEL: define dso_local double @discard_imaginary(
// CHECK: ret double %value.real

void use_complex_objects(complex_double value, unsigned index) {
  complex_double automatic = value;
  global_member.value = automatic;
  global_array[index] = automatic;
  volatile_global = automatic;
  automatic = volatile_global;
  global_float = (complex_float)automatic;
}

// CHECK-LABEL: define dso_local void @use_complex_objects(
// CHECK: store double %{{[^,]+}}, ptr getelementptr inbounds nuw (i8, ptr @global_member, i64 8), align 8
// CHECK: getelementptr inbounds nuw [2 x { double, double }], ptr @global_array
// CHECK: store volatile double %{{[^,]+}}, ptr @volatile_global, align 8
// CHECK: store volatile double %{{[^,]+}}, ptr getelementptr inbounds nuw (i8, ptr @volatile_global, i64 8), align 8
// CHECK: load volatile double, ptr @volatile_global, align 8
// CHECK: load volatile double, ptr getelementptr inbounds nuw (i8, ptr @volatile_global, i64 8), align 8
// CHECK: fptrunc double %{{[^ ]+}} to float
// CHECK: store float %{{[^,]+}}, ptr @global_float, align 4
// CHECK: store float %{{[^,]+}}, ptr getelementptr inbounds nuw (i8, ptr @global_float, i64 4), align 4
