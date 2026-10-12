// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fcxx-exceptions -fexceptions -O0 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fcxx-exceptions -fexceptions -O2 -emit-llvm -o - %s | FileCheck %s

typedef struct FILE FILE;
typedef __builtin_va_list va_list;
typedef __SIZE_TYPE__ size_t;
extern "C" {
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int vprintf(const char *, va_list);
int vfprintf(FILE *, const char *, va_list);
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int vsprintf(char *, const char *, va_list);
int vsnprintf(char *, size_t, const char *, va_list);
}

struct Guard {
  ~Guard();
};

// Stream I/O can propagate forced unwind at a cancellation point. Its builtin
// attributes must not discard the caller's automatic cleanup.
// CHECK-LABEL: define {{.*}}@library_streams(
// CHECK: invoke {{.*}}@printf(
// CHECK: invoke {{.*}}@fprintf(
// CHECK: invoke {{.*}}@vprintf(
// CHECK: invoke {{.*}}@vfprintf(
// CHECK: landingpad
// CHECK: cleanup
// CHECK: call {{.*}}@_ZN5GuardD1Ev(
extern "C" void library_streams(FILE *stream, const char *format, va_list args) {
  Guard guard;
  printf(format, 1);
  fprintf(stream, format, 1);
  vprintf(format, args);
  vfprintf(stream, format, args);
}

// CHECK-LABEL: define {{.*}}@builtin_streams(
// CHECK: invoke {{.*}}@printf(
// CHECK: invoke {{.*}}@fprintf(
// CHECK: invoke {{.*}}@vprintf(
// CHECK: invoke {{.*}}@vfprintf(
// CHECK: landingpad
// CHECK: cleanup
// CHECK: call {{.*}}@_ZN5GuardD1Ev(
extern "C" void builtin_streams(FILE *stream, const char *format, va_list args) {
  Guard guard;
  __builtin_printf(format, 1);
  __builtin_fprintf(stream, format, 1);
  __builtin_vprintf(format, args);
  __builtin_vfprintf(stream, format, args);
}

static int variadic_helper(FILE *stream, const char *format, ...) {
  va_list args;
  __builtin_va_start(args, format);
  int result = vfprintf(stream, format, args);
  __builtin_va_end(args);
  return result;
}

// The optimizer must not infer nounwind for a wrapper and then remove its
// caller's cleanup, even when the wrapper has no cleanup of its own.
// CHECK-LABEL: define {{.*}}@helper_user(
// CHECK: invoke {{.*}}@{{.*}}variadic_helper{{.*}}(
// CHECK: landingpad
// CHECK: cleanup
// CHECK: call {{.*}}@_ZN5GuardD1Ev(
extern "C" int helper_user(FILE *stream, const char *format, const char *value) {
  Guard guard;
  return variadic_helper(stream, format, value);
}

// Pure memory formatting retains its no-throw contract.
// CHECK-LABEL: define {{.*}}@memory_formats(
// CHECK: call {{.*}}@sprintf(
// CHECK: call {{.*}}@snprintf(
// CHECK: call {{.*}}@vsprintf(
// CHECK: call {{.*}}@vsnprintf(
// CHECK-NOT: landingpad
// CHECK: ret void
extern "C" void memory_formats(char *buffer, size_t size, const char *format,
                               va_list args) {
  Guard guard;
  sprintf(buffer, format, 1);
  snprintf(buffer, size, format, 1);
  vsprintf(buffer, format, args);
  vsnprintf(buffer, size, format, args);
}

// CHECK-LABEL: define {{.*}}@builtin_memory_formats(
// CHECK: call {{.*}}@sprintf(
// CHECK: call {{.*}}@snprintf(
// CHECK: call {{.*}}@vsprintf(
// CHECK: call {{.*}}@vsnprintf(
// CHECK-NOT: landingpad
// CHECK: ret void
extern "C" void builtin_memory_formats(char *buffer, size_t size,
                                       const char *format, va_list args) {
  Guard guard;
  __builtin_sprintf(buffer, format, 1);
  __builtin_snprintf(buffer, size, format, 1);
  __builtin_vsprintf(buffer, format, args);
  __builtin_vsnprintf(buffer, size, format, args);
}
