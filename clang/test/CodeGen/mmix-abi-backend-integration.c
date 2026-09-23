// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-unknown -std=gnu2x \
// RUN:   -mrelocation-model static -emit-llvm -disable-llvm-passes \
// RUN:   -o %t.ll %s
// RUN: FileCheck %s --check-prefix=IR < %t.ll
// RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=asm \
// RUN:   %t.ll -o - | FileCheck %s --check-prefix=ASM
// RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=obj \
// RUN:   %t.ll -o %t.o
// RUN: llvm-readobj --file-headers --sections --symbols \
// RUN:   --relocations --expand-relocs %t.o \
// RUN:   | FileCheck %s --check-prefix=ELF
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=OBJ --implicit-check-not='<unknown>'

struct Direct {
  int word;
};

struct Large {
  long words[3];
};

typedef long (*callback_type)(long);

long global_seed = 7;
long *global_pointer = &global_seed;

extern long external_scalar(signed char, unsigned short, long *);
extern struct Direct external_direct(struct Direct, struct Large);
extern long external_variadic(long, ...);

static long recursive_sum(long value) {
  if (value <= 0)
    return global_seed;
  return value + recursive_sum(value - 1);
}

struct Large compose(signed char signed_byte, unsigned short unsigned_half,
                     struct Direct direct, struct Large copy, long *pointer,
                     callback_type callback, long named, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, named);
  long unnamed = __builtin_va_arg(ap, long);
  __builtin_va_end(ap);

  long recursive = recursive_sum(named);
  long indirect = callback(unnamed);
  long scalar = external_scalar(signed_byte, unsigned_half, pointer);
  struct Direct returned = external_direct(direct, copy);
  long variadic = external_variadic(named, direct, copy);

  copy.words[0] = recursive + indirect + scalar + returned.word + variadic;
  copy.words[1] = *global_pointer;
  return copy;
}

void copy_block(void *destination, const void *source) {
  __builtin_memcpy(destination, source, 128);
}

// IR: %struct.Large = type { [3 x i64] }
// IR: %struct.Direct = type { i32 }
// IR: @global_seed ={{.*}} global i64 7, align 8
// IR: @global_pointer ={{.*}} global ptr @global_seed, align 8

// IR-LABEL: define dso_local void @compose(
// IR-SAME: ptr dead_on_unwind noalias writable sret(%struct.Large) align 8 %agg.result,
// IR-SAME: i8 noundef signext %signed_byte,
// IR-SAME: i16 noundef zeroext %unsigned_half,
// IR-SAME: i32 noext %direct.coerce,
// IR-SAME: ptr noundef byval(%struct.Large) align 8 %copy,
// IR-SAME: ptr noundef %pointer, ptr noundef %callback,
// IR-SAME: i64 noundef %named, ...)
// IR: call void @llvm.va_start.p0(
// IR: getelementptr inbounds i8, ptr %{{[a-z0-9.]+}}, i64 8
// IR: call void @llvm.va_end.p0(
// IR: call i64 @recursive_sum(i64 noundef %{{[a-z0-9.]+}})
// IR: call i64 %{{[0-9]+}}(i64 noundef %{{[a-z0-9.]+}})
// IR: call i64 @external_scalar(i8 noundef signext %{{[0-9]+}}, i16 noundef zeroext %{{[0-9]+}}, ptr noundef %{{[0-9]+}})
// IR: call i32 @external_direct(i32 noext %{{[0-9]+}}, ptr noundef byval(%struct.Large) align 8 %copy)
// IR: call i64 (i64, ...) @external_variadic(i64 noundef %{{[0-9]+}}, i32 noext %{{[0-9]+}}, ptr noundef byval(%struct.Large) align 8 %copy)
// IR: load ptr, ptr @global_pointer, align 8
// IR: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %agg.result, ptr align 8 %copy, i64 24, i1 false)

// IR-LABEL: define internal i64 @recursive_sum(i64 noundef %value)
// IR: load i64, ptr @global_seed, align 8
// IR: call i64 @recursive_sum(i64 noundef %sub)

// IR-LABEL: define dso_local void @copy_block(
// IR: call void @llvm.memcpy.p0.p0.i64({{.*}}i64 128, i1 false)

// ASM-LABEL: compose:
// ASM: PUSHJ {{r[0-9]+}}, recursive_sum
// ASM: PUSHGO {{r[0-9]+}}, {{r[0-9]+}}, 0
// ASM: GETA {{r[0-9]+}}, %geta(external_scalar)
// ASM: PUSHGO {{r[0-9]+}}, {{r[0-9]+}}, 0
// ASM: GETA {{r[0-9]+}}, %geta(external_direct)
// ASM: GETA {{r[0-9]+}}, %geta(external_variadic)
// ASM: GETA {{r[0-9]+}}, %geta(global_pointer)
// ASM-LABEL: recursive_sum:
// ASM: PUSHJB {{r[0-9]+}}, recursive_sum
// ASM-LABEL: copy_block:
// ASM: GETA {{r[0-9]+}}, %geta(memcpy)
// ASM: PUSHGO {{r[0-9]+}}, {{r[0-9]+}}, 0

// ELF: Format: elf64-mmix
// ELF: Arch: mmix
// ELF: Name: .text
// ELF: Type: SHT_PROGBITS
// ELF: Name: .rela.text
// ELF: Type: SHT_RELA
// ELF: Name: .data
// ELF: Type: SHT_PROGBITS
// ELF: Name: .rela.data
// ELF: Type: SHT_RELA
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: external_scalar
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: external_direct
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: external_variadic
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: global_pointer
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: global_seed
// ELF: Type: R_MMIX_GETA (13)
// ELF-NEXT: Symbol: memcpy
// ELF: Type: R_MMIX_64 (5)
// ELF-NEXT: Symbol: global_seed
// ELF: Name: recursive_sum
// ELF: Binding: Local
// ELF-NEXT: Type: Function
// ELF: Name: compose
// ELF: Binding: Global
// ELF: Type: Function
// ELF: Name: external_scalar
// ELF: Section: Undefined
// ELF: Name: global_seed
// ELF: Type: Object
// ELF: Name: memcpy
// ELF: Section: Undefined

// OBJ-LABEL: <compose>:
// OBJ: PUSHJ {{r[0-9]+}},
// OBJ: PUSHGO {{r[0-9]+}}, {{r[0-9]+}}, 0
// OBJ: GETA {{r[0-9]+}}, 0
// OBJ-NEXT: {{.*}} R_MMIX_GETA external_scalar
// OBJ: PUSHGO r31, {{r[0-9]+}}, 0
// OBJ: GETA {{r[0-9]+}}, 0
// OBJ-NEXT: {{.*}} R_MMIX_GETA external_direct
// OBJ: PUSHGO r31, {{r[0-9]+}}, 0
// OBJ: GETA {{r[0-9]+}}, 0
// OBJ-NEXT: {{.*}} R_MMIX_GETA external_variadic
// OBJ: PUSHGO r31, {{r[0-9]+}}, 0
// OBJ: GETA {{r[0-9]+}}, 0
// OBJ-NEXT: {{.*}} R_MMIX_GETA global_pointer
// OBJ-LABEL: <copy_block>:
// OBJ: GETA {{r[0-9]+}}, 0
// OBJ-NEXT: {{.*}} R_MMIX_GETA memcpy
// OBJ: PUSHGO r31, {{r[0-9]+}}, 0
