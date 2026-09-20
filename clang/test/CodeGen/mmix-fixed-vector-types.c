// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=c17 \
// RUN:   -mrelocation-model static -emit-llvm -o - \
// RUN:   -DTEST_SUPPORTED %s | FileCheck %s --check-prefix=SUPPORTED
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_WIDE %s 2>&1 | FileCheck %s --check-prefix=WIDE
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_WIDE_MASK %s 2>&1 | FileCheck %s --check-prefix=WIDE-MASK
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_ODD_LANES %s 2>&1 | FileCheck %s --check-prefix=ODD
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_BITINT_LANE %s 2>&1 | FileCheck %s --check-prefix=BITINT
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_OVERALIGNED %s 2>&1 | FileCheck %s --check-prefix=OVERALIGNED
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null \
// RUN:   -DTEST_ATOMIC_VECTOR %s 2>&1 | FileCheck %s --check-prefix=ATOMIC

typedef _Bool mask8 __attribute__((ext_vector_type(8)));
typedef _Bool mask64 __attribute__((ext_vector_type(64)));
typedef signed char i8x1 __attribute__((ext_vector_type(1)));
typedef signed char i8x2 __attribute__((ext_vector_type(2)));
typedef short i16x2 __attribute__((ext_vector_type(2)));
typedef int i32x2 __attribute__((ext_vector_type(2)));
typedef unsigned int u32x2 __attribute__((vector_size(8)));
typedef long i64x1 __attribute__((ext_vector_type(1)));
typedef float f32x2 __attribute__((ext_vector_type(2)));
typedef double f64x1 __attribute__((ext_vector_type(1)));
typedef long double ld64x1 __attribute__((ext_vector_type(1)));

#if defined(TEST_SUPPORTED)
mask8 masks = {1, 0, 1, 0, 1, 0, 1, 0};
mask64 wide_masks;
i8x1 byte = {1};
i8x2 bytes = {1, 2};
i16x2 halves = {3, 4};
i32x2 words = {5, 6};
u32x2 unsigned_words = {7, 8};
i64x1 octa = {7};
f32x2 singles = {1.0f, 2.0f};
f64x1 doubles = {3.0};
ld64x1 long_doubles = {4.0L};

_Static_assert(sizeof(mask8) == 1 && _Alignof(mask8) == 1, "mask8 ABI");
_Static_assert(sizeof(mask64) == 8 && _Alignof(mask64) == 8, "mask64 ABI");
_Static_assert(sizeof(i8x1) == 1 && _Alignof(i8x1) == 1, "i8x1 ABI");
_Static_assert(sizeof(i32x2) == 8 && _Alignof(i32x2) == 8, "i32x2 ABI");

void copy_supported_vectors(void) {
  i32x2 local = words;
  words = local;
}

struct vector_record {
  i32x2 field;
};
struct vector_record record;
i32x2 array[2];

// SUPPORTED: @masks ={{.*}} global <8 x i1>
// SUPPORTED: @byte ={{.*}} global <1 x i8>
// SUPPORTED: @bytes ={{.*}} global <2 x i8>
// SUPPORTED: @halves ={{.*}} global <2 x i16>
// SUPPORTED: @words ={{.*}} global <2 x i32>
// SUPPORTED: @unsigned_words ={{.*}} global <2 x i32>
// SUPPORTED: @octa ={{.*}} global <1 x i64>
// SUPPORTED: @singles ={{.*}} global <2 x float>
// SUPPORTED: @doubles ={{.*}} global <1 x double>
// SUPPORTED: @long_doubles ={{.*}} global <1 x double>
// SUPPORTED: @wide_masks ={{.*}} global i64 0, align 8
// SUPPORTED: @record ={{.*}} global %struct.vector_record zeroinitializer, align 8
// SUPPORTED: @array ={{.*}} global [2 x <2 x i32>] zeroinitializer, align 8
// SUPPORTED-LABEL: define dso_local void @copy_supported_vectors()
// SUPPORTED: load <2 x i32>, ptr @words, align 8
// SUPPORTED: store <2 x i32>
#elif defined(TEST_WIDE)
typedef int i32x16 __attribute__((ext_vector_type(16)));
i32x16 wide;
// WIDE: error: MMIX GNU ABI does not support vector value CodeGen involving type 'i32x16'
#elif defined(TEST_WIDE_MASK)
typedef _Bool mask128 __attribute__((ext_vector_type(128)));
mask128 wide_mask;
// WIDE-MASK: error: MMIX GNU ABI does not support vector value CodeGen involving type 'mask128'
#elif defined(TEST_ODD_LANES)
typedef short i16x3 __attribute__((ext_vector_type(3)));
i16x3 odd;
// ODD: error: MMIX GNU ABI does not support vector value CodeGen involving type 'i16x3'
#elif defined(TEST_BITINT_LANE)
typedef _BitInt(32) bitint2 __attribute__((ext_vector_type(2)));
bitint2 bitint_lanes;
// BITINT: error: MMIX GNU ABI does not support vector value CodeGen involving type 'bitint2'
#elif defined(TEST_OVERALIGNED)
typedef int aligned_i32x2 __attribute__((ext_vector_type(2), aligned(16)));
aligned_i32x2 over_aligned;
// OVERALIGNED: error: MMIX GNU ABI does not support vector value CodeGen involving type 'aligned_i32x2'
#elif defined(TEST_ATOMIC_VECTOR)
typedef _Atomic(i32x2) atomic_i32x2;
atomic_i32x2 atomic_vector;
// ATOMIC: error: MMIX GNU ABI does not support vector value CodeGen involving type 'atomic_i32x2'
#endif
