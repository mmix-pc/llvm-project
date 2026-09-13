// Invalid identity restatements are owned by the target-triple validation.
// RUN: not %clang --target=mmix-pc-unknown -ffreestanding -fsyntax-only \
// RUN:   %S/Inputs/machine-model-invalid.c 2>&1 \
// RUN:   | FileCheck %s --check-prefix=VENDOR
// RUN: not %clang --target=mmix-unknown-freebsd -ffreestanding -fsyntax-only \
// RUN:   %S/Inputs/machine-model-invalid.c 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OS
// RUN: not %clang --target=mmix-unknown-unknown-elf -ffreestanding \
// RUN:   -fsyntax-only %S/Inputs/machine-model-invalid.c 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ENVIRONMENT

// ABI and output-model restatements must not silently change the frozen
// static, small, 64-bit model.
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -mabi=gnu \
// RUN:   -fsyntax-only %S/Inputs/machine-model-invalid.c 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ABI
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding \
// RUN:   -mcmodel=large -fsyntax-only %S/Inputs/machine-model-invalid.c 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CODE-MODEL
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -fPIC -S \
// RUN:   %S/Inputs/machine-model-invalid.c -o %t.pic.s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PIC
// RUN: not test -s %t.pic.s
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -fPIE -c \
// RUN:   %S/Inputs/machine-model-invalid.c -o %t.pie.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PIC
// RUN: not test -s %t.pie.o

// Source types that cannot cross or inhabit the frozen C machine model retain
// their target or frontend-owned diagnostics through the public Driver path.
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_HALF -fsyntax-only %S/Inputs/machine-model-invalid.c 2>&1 \
// RUN:   | FileCheck %s --check-prefix=HALF
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_WIDE_ABI -S -emit-llvm %S/Inputs/machine-model-invalid.c \
// RUN:   -o %t.wide.ll 2>&1 | FileCheck %s --check-prefix=WIDE
// RUN: not test -s %t.wide.ll
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -DTEST_ADDRESS_SPACE -S -emit-llvm \
// RUN:   %S/Inputs/machine-model-invalid.c -o %t.as.ll 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ADDRESS-SPACE
// RUN: not test -s %t.as.ll

// VENDOR: error: unknown target triple 'mmix-pc-unknown'
// OS: error: unknown target triple 'mmix-unknown-freebsd'
// ENVIRONMENT: error: unknown target triple 'mmix-unknown-unknown-elf'
// ABI: error: unsupported option '-mabi=' for target 'mmix-unknown-unknown'
// CODE-MODEL: error: unsupported argument 'large' to option '-mcmodel=' for target 'mmix-unknown-unknown'
// PIC: fatal error: error in backend: MMIX supports only the static relocation model
// HALF: error: _Float16 is not supported on this target
// WIDE-COUNT-2: error: signed _BitInt of bit sizes greater than 64 not supported
// ADDRESS-SPACE: error: MMIX GNU ABI does not support return type 'as1_int *'
// ADDRESS-SPACE: error: MMIX GNU ABI does not support argument type 'as1_int *'
