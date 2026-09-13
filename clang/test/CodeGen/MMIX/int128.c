// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-linux -std=c17 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-unknown -std=c17 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-linux -std=c17 -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=OPT
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-unknown -std=c17 -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=OPT
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-linux -std=c17 -O0 -emit-obj -o %t.linux.o0.o %s
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-linux -std=c17 -O2 -emit-obj -o %t.linux.o2.o %s
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-unknown -std=c17 -O0 -emit-obj -o %t.generic.o0.o %s
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-unknown -std=c17 -O2 -emit-obj -o %t.generic.o2.o %s

typedef __int128 I;
typedef unsigned __int128 U;
_Static_assert(__SIZEOF_INT128__ == 16, "feature macro");
_Static_assert(sizeof(I) == 16 && _Alignof(I) == 8, "signed layout");
_Static_assert(sizeof(U) == 16 && _Alignof(U) == 8, "unsigned layout");
_Static_assert((I)-1 < 0 && (U)-1 > 0, "signedness");
_Static_assert(((U)1 << 127) >> 127 == 1, "constant evaluation");
_Static_assert(!__atomic_always_lock_free(16, 0), "no native wide atomics");
struct Record { char tag; I value; };
struct __attribute__((packed)) Packed { char tag; U value; };
struct Wrapper { I value; };
union Union { char tag; U value; };
_Static_assert(__builtin_offsetof(struct Record, value) == 8 &&
               sizeof(struct Record) == 24, "record layout");
_Static_assert(__builtin_offsetof(struct Packed, value) == 1 &&
               sizeof(struct Packed) == 17, "packed layout");
_Static_assert(sizeof(union Union) == 16 && _Alignof(union Union) == 8,
               "union layout");

// CHECK-LABEL: define{{.*}} i128 @identity(i128 noundef
// OPT-LABEL: define{{.*}} i128 @identity(i128 noundef
// OPT: ret i128 %value
I identity(I value) { return value; }
// CHECK-LABEL: define{{.*}} i128 @unsigned_identity(i128 noundef
// OPT-LABEL: define{{.*}} i128 @unsigned_identity(i128 noundef
// OPT: ret i128 %value
U unsigned_identity(U value) { return value; }
// CHECK-LABEL: define{{.*}} i128 @indirect(
// CHECK: call i128 %{{.*}}(i128 noundef
I indirect(I (*fn)(I), I value) { return fn(value); }
// CHECK-LABEL: define{{.*}} void @aggregate(ptr {{.*}}sret(%struct.Wrapper) align 8{{.*}}, ptr {{.*}}byval(%struct.Wrapper) align 8
// OPT-LABEL: define{{.*}} void @aggregate(ptr {{.*}}sret(%struct.Wrapper) align 8{{.*}}, ptr {{.*}}byval(%struct.Wrapper) align 8
struct Wrapper aggregate(struct Wrapper value) { return value; }

// CHECK-LABEL: define{{.*}} i128 @packed_load(
// CHECK: load i128, ptr {{.*}}, align 1
U packed_load(struct Packed *p) { return p->value; }

// i128 is a direct scalar, not an aggregate pointer. Each va_arg advances
// exactly two octa slots; there is no 16-byte alignment or right adjustment.
// CHECK-LABEL: define{{.*}} i128 @variadic(i64 noundef %tag, ...)
// CHECK: call void @llvm.va_start
// CHECK: call void @llvm.va_copy
// CHECK: [[SIGNED:%.*]] = load ptr, ptr
// CHECK-NEXT: [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[SIGNED]], i64 16
// CHECK-NEXT: store ptr [[NEXT]], ptr
// CHECK-NEXT: {{%.*}} = load i128, ptr [[SIGNED]], align 8
// CHECK: [[UNSIGNED:%.*]] = load ptr, ptr
// CHECK-NEXT: [[NEXTU:%.*]] = getelementptr inbounds i8, ptr [[UNSIGNED]], i64 16
// CHECK-NEXT: store ptr [[NEXTU]], ptr
// CHECK-NEXT: {{%.*}} = load i128, ptr [[UNSIGNED]], align 8
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i64 8
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i64 16
// CHECK: call void @llvm.va_end
I variadic(long tag, ...) {
  __builtin_va_list ap, copy;
  __builtin_va_start(ap, tag);
  __builtin_va_copy(copy, ap);
  I first = __builtin_va_arg(ap, I);
  U second = __builtin_va_arg(ap, U);
  long third = __builtin_va_arg(ap, long);
  I again = __builtin_va_arg(copy, I);
  __builtin_va_end(copy);
  __builtin_va_end(ap);
  return first + (I)second + third + again;
}

extern I boundary(long, long, long, long, long, long, long, long,
                  long, long, long, long, long, long, long, ...);
// The high word uses the last argument register; the low word and the
// following arguments continue on the stack (covered independently in llc).
// CHECK-LABEL: define{{.*}} i128 @call_boundary(
// CHECK: call i128 (i64, {{.*}}...) @boundary({{.*}}i128 noundef {{.*}}, i128 noundef {{.*}}, i64 noundef 17)
I call_boundary(I first, U second) {
  return boundary(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                  first, second, 17L);
}

// CHECK-LABEL: define{{.*}} i128 @named_wide(i128 noundef %named, ...)
// CHECK: call void @llvm.va_start
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i64 16
I named_wide(I named, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, named);
  I next = __builtin_va_arg(ap, I);
  __builtin_va_end(ap);
  return named + next;
}
