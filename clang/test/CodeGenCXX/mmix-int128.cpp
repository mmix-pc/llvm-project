// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-linux -std=c++17 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-unknown -std=c++17 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-linux -std=c++17 -O0 -emit-obj -o %t.linux.o0.o %s
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-linux -std=c++17 -O2 -emit-obj -o %t.linux.o2.o %s
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-unknown -std=c++17 -O0 -emit-obj -o %t.generic.o0.o %s
// RUN: %clang_cc1 -mrelocation-model static -triple mmix-unknown-unknown -std=c++17 -O2 -emit-obj -o %t.generic.o2.o %s

using I = __int128;
using U = unsigned __int128;
static_assert(__SIZEOF_INT128__ == 16);
static_assert(sizeof(I) == 16 && alignof(I) == 8);
static_assert(sizeof(U) == 16 && alignof(U) == 8);
static_assert(__is_integral(I) && __is_signed(I));
static_assert(__is_integral(U) && __is_unsigned(U));
static_assert(!__is_same(I, long long));
constexpr U high = U(1) << 127;
static_assert((high >> 127) == 1 && (I(-1) >> 100) == -1);
static_assert(U(I(-1)) == ~U(0));
enum class Wide : U { High = high };
static_assert(sizeof(Wide) == 16 && alignof(Wide) == 8);

// Itanium encodes signed/unsigned __int128 as n/o, without a target suffix.
// CHECK-LABEL: define{{.*}} i128 @_Z8identityn(i128 noundef
I identity(I value) { return value; }
// CHECK-LABEL: define{{.*}} i128 @_Z8identityo(i128 noundef
U identity(U value) { return value; }
// CHECK-LABEL: define{{.*}} i128 @_Z4wide4Wide(i128 noundef
Wide wide(Wide value) { return value; }

template <I N> I constant() { return N; }
template I constant<42>();
// CHECK-LABEL: define{{.*}} i128 @_Z8constantILn42EEnv()

// CHECK-LABEL: define{{.*}} i128 @_Z9from_addrPv(
// CHECK: ptrtoint ptr {{.*}} to i128
I from_addr(void *p) { return reinterpret_cast<I>(p); }
// CHECK-LABEL: define{{.*}} ptr @_Z7to_addro(i128 noundef
// CHECK: [[NARROW:%.*]] = trunc i128 {{.*}} to i64
// CHECK: inttoptr i64 [[NARROW]] to ptr
void *to_addr(U value) { return reinterpret_cast<void *>(value); }
