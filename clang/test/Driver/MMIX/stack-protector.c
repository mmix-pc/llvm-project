// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -O0 \
// RUN:   -fstack-protector -S -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=BASE
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -O0 \
// RUN:   -fstack-protector-strong -S -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=STRONG
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -O0 \
// RUN:   -fstack-protector-all -S -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=ALL
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -O1 \
// RUN:   -fstack-protector -S %s -o - | FileCheck %s --check-prefix=ASM-BASE
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -O1 \
// RUN:   -fstack-protector-strong -S %s -o - \
// RUN:   | FileCheck %s --check-prefix=ASM-STRONG
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -O1 \
// RUN:   -fstack-protector-all -S %s -o - \
// RUN:   | FileCheck %s --check-prefix=ASM-ALL
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -O1 \
// RUN:   -fstack-protector -c %s -o %t.o
// RUN: llvm-readobj --relocations %t.o | FileCheck %s --check-prefix=RELOC
// RUN: llvm-nm --undefined-only %t.o | FileCheck %s --check-prefix=SYMBOL
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding \
// RUN:   -mstack-protector-guard=global -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=GUARD-GLOBAL
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding \
// RUN:   -mstack-protector-guard=tls -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=GUARD-TLS
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding \
// RUN:   -mstack-protector-guard-symbol=custom_guard -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=GUARD-SYMBOL

extern void escape(void *);

int plain(int value) { return value + 1; }

int small_array(int value) {
  char buffer[4];
  buffer[0] = value;
  escape(buffer);
  return buffer[0];
}

int large_array(int value) {
  char buffer[16];
  buffer[0] = value;
  escape(buffer);
  return buffer[0];
}

int address_taken(int value) {
  int local = value;
  escape(&local);
  return local;
}

// BASE: define dso_local i32 @plain({{.*}}) #[[BASE_ATTR:[0-9]+]]
// BASE: define dso_local i32 @small_array({{.*}}) #[[BASE_ATTR]]
// BASE: define dso_local i32 @large_array({{.*}}) #[[BASE_ATTR]]
// BASE: define dso_local i32 @address_taken({{.*}}) #[[BASE_ATTR]]
// BASE: attributes #[[BASE_ATTR]] = {{.*}} ssp

// STRONG: define dso_local i32 @plain({{.*}}) #[[STRONG_ATTR:[0-9]+]]
// STRONG: define dso_local i32 @small_array({{.*}}) #[[STRONG_ATTR]]
// STRONG: define dso_local i32 @large_array({{.*}}) #[[STRONG_ATTR]]
// STRONG: define dso_local i32 @address_taken({{.*}}) #[[STRONG_ATTR]]
// STRONG: attributes #[[STRONG_ATTR]] = {{.*}} sspstrong

// ALL: define dso_local i32 @plain({{.*}}) #[[ALL_ATTR:[0-9]+]]
// ALL: define dso_local i32 @small_array({{.*}}) #[[ALL_ATTR]]
// ALL: define dso_local i32 @large_array({{.*}}) #[[ALL_ATTR]]
// ALL: define dso_local i32 @address_taken({{.*}}) #[[ALL_ATTR]]
// ALL: attributes #[[ALL_ATTR]] = {{.*}} sspreq

// ASM-BASE-LABEL: plain:
// ASM-BASE-NOT: __stack_chk
// ASM-BASE-LABEL: small_array:
// ASM-BASE-NOT: __stack_chk
// ASM-BASE-LABEL: large_array:
// ASM-BASE: GETA {{r[0-9]+}}, %geta(__stack_chk_guard)
// ASM-BASE: LDOU {{r[0-9]+}}, {{r[0-9]+}}, 0
// ASM-BASE: LDOU {{r[0-9]+}}, {{r[0-9]+}}, 0
// ASM-BASE: GETA {{r[0-9]+}}, %geta(__stack_chk_fail)
// ASM-BASE: PUSHGO
// ASM-BASE-LABEL: address_taken:
// ASM-BASE-NOT: __stack_chk

// ASM-STRONG-LABEL: plain:
// ASM-STRONG-NOT: __stack_chk
// ASM-STRONG-LABEL: small_array:
// ASM-STRONG: __stack_chk_guard
// ASM-STRONG-LABEL: large_array:
// ASM-STRONG: __stack_chk_guard
// ASM-STRONG-LABEL: address_taken:
// ASM-STRONG: __stack_chk_guard

// ASM-ALL-LABEL: plain:
// ASM-ALL: __stack_chk_guard
// ASM-ALL-LABEL: small_array:
// ASM-ALL: __stack_chk_guard
// ASM-ALL-LABEL: large_array:
// ASM-ALL: __stack_chk_guard
// ASM-ALL-LABEL: address_taken:
// ASM-ALL: __stack_chk_guard

// RELOC-DAG: R_MMIX_GETA __stack_chk_guard
// RELOC-DAG: R_MMIX_GETA __stack_chk_fail
// SYMBOL-DAG: U __stack_chk_fail
// SYMBOL-DAG: U __stack_chk_guard

// GUARD-GLOBAL: error: unsupported option '-mstack-protector-guard=global' for target 'mmix-unknown-unknown'
// GUARD-TLS: error: unsupported option '-mstack-protector-guard=tls' for target 'mmix-unknown-unknown'
// GUARD-SYMBOL: error: unsupported option '-mstack-protector-guard-symbol=custom_guard' for target 'mmix-unknown-unknown'
