// REQUIRES: mmix-registered-target
// RUN: %clang --target=mmix -O0 -ffreestanding -fno-stack-protector \
// RUN:   -gdwarf-5 -g -ffile-prefix-map=%S=/src \
// RUN:   -fdebug-compilation-dir=/build -c %s -o %t.o
// RUN: ld.lld -m elf64mmix -e frame_state %t.o -o %t
// RUN: llvm-dwarfdump --verify %t
// RUN: llvm-dwarfdump --debug-info %t | FileCheck %s --check-prefix=INFO
// RUN: llvm-dwarfdump --debug-frame %t | FileCheck %s --check-prefix=FRAME
// RUN: llvm-readobj --sections %t \
// RUN:   | FileCheck %s --check-prefix=SECTIONS --implicit-check-not=.eh_frame
// RUN: %clang --target=mmix -O0 -ffreestanding -fno-stack-protector \
// RUN:   -c %s -o %t.nodebug.o
// RUN: llvm-readobj --sections %t.nodebug.o | FileCheck %s \
// RUN:   --check-prefix=NODEBUG --implicit-check-not=.debug_frame \
// RUN:   --implicit-check-not=.eh_frame

// INFO: DW_TAG_subprogram
// INFO: DW_AT_frame_base (DW_OP_reg29 R253)
// INFO-NEXT: DW_AT_name ("frame_state")
// INFO: DW_AT_location (DW_OP_fbreg
// INFO-NEXT: DW_AT_name ("count")
// INFO: DW_AT_location (DW_OP_fbreg
// INFO-NEXT: DW_AT_name ("fixed")
// INFO: DW_AT_name ("__vla_expr0")
// INFO: DW_TAG_variable
// INFO-NEXT: DW_AT_location
// INFO-NEXT: {{.*}}DW_OP_breg{{[0-9]+}} R{{[0-9]+}}+0)
// INFO-NEXT: DW_AT_name ("dynamic")

// SECTIONS-DAG: Name: .debug_info
// SECTIONS-DAG: Name: .debug_loclists
// SECTIONS-DAG: Name: .debug_frame

// FRAME: Return address column: 35
// FRAME: CFA=R254
// FRAME: CFA=R253: R253=[CFA-{{[0-9]+}}], RJ=[CFA-{{[0-9]+}}]
// Restore FP before releasing its save area; rJ remains in a preserved local.
// FRAME: CFA=R254+[[SIZE:[0-9]+]]: R253=[CFA-{{[0-9]+}}], RJ=R30
// FRAME: CFA=R254+[[SIZE]]: R253=same, RJ=R30
// FRAME: CFA=R254: R253=same, RJ=R30
// FRAME: CFA=R254: R253=same, RJ=same

// NODEBUG: Format: elf64-mmix

__attribute__((noinline)) static long frame_callee(long value) {
  return value + 3;
}

__attribute__((noinline)) long frame_state(long count) {
  long fixed = frame_callee(count) + 1;
  volatile long dynamic[count];
  dynamic[0] = fixed;
  return dynamic[0];
}
