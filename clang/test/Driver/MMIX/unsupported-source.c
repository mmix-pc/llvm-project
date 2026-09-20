// Unsupported source and ABI forms must stop at their current semantic owner
// before the public Driver can claim assembly or object output.

// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_HALF -c %S/Inputs/unsupported-source.c -o %t.half.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=HALF
// RUN: not test -s %t.half.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_BITINT -c %S/Inputs/unsupported-source.c -o %t.bitint.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BITINT
// RUN: not test -s %t.bitint.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_COMPLEX -c %S/Inputs/unsupported-source.c \
// RUN:   -o %t.complex.o 2>&1 | FileCheck %s --check-prefix=COMPLEX
// RUN: not test -s %t.complex.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_VECTOR -c %S/Inputs/unsupported-source.c -o %t.vector.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=VECTOR
// RUN: not test -s %t.vector.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_ATOMIC_OPERATION -c %S/Inputs/unsupported-source.c \
// RUN:   -o %t.atomic-operation.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ATOMIC-OPERATION
// RUN: not test -s %t.atomic-operation.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_ATOMIC_RMW_OPERATOR -c %S/Inputs/unsupported-source.c \
// RUN:   -o %t.atomic-rmw.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ATOMIC-RMW
// RUN: not test -s %t.atomic-rmw.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_ATOMIC_EXTENSION -c %S/Inputs/unsupported-source.c \
// RUN:   -o %t.atomic-extension.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ATOMIC-EXTENSION
// RUN: not test -s %t.atomic-extension.o

// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_OVERALIGNED_ARGUMENT -c %S/Inputs/unsupported-source.c \
// RUN:   -o %t.overaligned-argument.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OVERALIGNED-ARGUMENT
// RUN: not test -s %t.overaligned-argument.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_OVERALIGNED_RESULT -c %S/Inputs/unsupported-source.c \
// RUN:   -o %t.overaligned-result.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OVERALIGNED-RESULT
// RUN: not test -s %t.overaligned-result.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_ADDRESS_SPACE -c %S/Inputs/unsupported-source.c \
// RUN:   -o %t.address-space.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ADDRESS-SPACE
// RUN: not test -s %t.address-space.o

// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_NAKED -c %S/Inputs/unsupported-source.c -o %t.naked.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NAKED
// RUN: not test -s %t.naked.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_TARGET_ATTRIBUTE -c %S/Inputs/unsupported-source.c \
// RUN:   -o %t.target.o 2>&1 | FileCheck %s --check-prefix=TARGET
// RUN: not test -s %t.target.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_MULTIVERSIONING -c %S/Inputs/unsupported-source.c \
// RUN:   -o %t.multiversioning.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MULTIVERSIONING
// RUN: not test -s %t.multiversioning.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_CALLING_CONVENTION -c %S/Inputs/unsupported-source.c \
// RUN:   -o %t.calling-convention.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CALLING-CONVENTION
// RUN: not test -s %t.calling-convention.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_TLS -c %S/Inputs/unsupported-source.c -o %t.tls.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TLS
// RUN: not test -s %t.tls.o

// Explicitly disabling exceptions retains the ordinary language diagnostic.
// RUN: not %clangxx --target=mmix-unknown-unknown -ffreestanding -std=c++17 \
// RUN:   -DTEST_EXCEPTION -fno-exceptions \
// RUN:   -c %S/Inputs/unsupported-source.cpp -o %t.exception.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EXCEPTION
// RUN: not test -s %t.exception.o

// HALF: error: _Float16 is not supported on this target
// BITINT: error: MMIX GNU ABI does not support extended scalar operation CodeGen involving type '_BitInt(17)'
// COMPLEX: error: MMIX GNU ABI does not support return type '_Complex int'
// COMPLEX: error: MMIX GNU ABI does not support argument type '_Complex int'
// VECTOR: error: MMIX GNU ABI does not support vector value CodeGen involving type 'int16'
// ATOMIC-OPERATION: error: MMIX GNU ABI does not support atomic operation __atomic_fetch_add
// ATOMIC-RMW: error: MMIX GNU ABI does not support atomic operation +=
// ATOMIC-EXTENSION: error: MMIX GNU ABI does not support atomic operation __atomic_fetch_uinc
// OVERALIGNED-ARGUMENT: error: MMIX GNU ABI does not support over-aligned aggregate argument type 'struct OverAligned'
// OVERALIGNED-RESULT: error: MMIX GNU ABI does not support over-aligned aggregate return type 'struct OverAligned'
// ADDRESS-SPACE: error: MMIX GNU ABI does not support nonzero-address-space value CodeGen involving type '__attribute__((address_space(1))) int'
// NAKED: error: MMIX does not support the 'naked' function attribute
// TARGET: error: MMIX does not support the 'target' function attribute
// MULTIVERSIONING: error: function multiversioning is not supported on the current target
// CALLING-CONVENTION: error: 'fastcall' calling convention is not supported for this target
// TLS: error: thread-local storage is not supported for the current target
// EXCEPTION: error: cannot use 'throw' with exceptions disabled
