// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 -O1 \
// RUN:   -c %s -o %t.o
// RUN: llvm-readobj --file-headers --symbols --relocations %t.o \
// RUN:   | FileCheck %s --check-prefix=ELF \
// RUN:   --implicit-check-not=__atomic_ --implicit-check-not=__sync_
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=DIS --implicit-check-not='<unknown>' \
// RUN:   --implicit-check-not=__atomic_ --implicit-check-not=__sync_

// ELF: Format: elf64-mmix
// ELF-NEXT: Arch: mmix
// ELF: Type: Relocatable
// ELF-NEXT: Machine: EM_MMIX (0x50)
// ELF: Relocations [
// ELF-NEXT: ]
// ELF: Name: c17_exchange_byte
// ELF: Type: Function
// ELF: Name: gnu_fetch_add_word
// ELF: Type: Function
// ELF: Name: sync_compare_word
// ELF: Type: Function
// ELF: Name: c17_system_fence
// ELF: Type: Function
// ELF-NOT: Section: Undefined

// A narrow exchange masks and reinserts its big-endian byte before the
// encoded immediate-form CSWAP retry.
// DIS-LABEL: <c17_exchange_byte>:
// DIS: ANDN
// DIS: LDOU
// DIS: AND
// DIS: OR
// DIS: CSWAP
// DIS: BNZB
// DIS-LABEL: <gnu_fetch_add_word>:
// DIS: SYNC 3
// DIS: LDOU
// DIS: ADDU
// DIS: CSWAP
// DIS: BNZB
// DIS: SYNC 3
// DIS-LABEL: <sync_compare_word>:
// DIS: SYNC 3
// DIS: CSWAP
// DIS: SYNC 3
// DIS-LABEL: <c17_system_fence>:
// DIS: SYNC 3

typedef unsigned char u8;
typedef unsigned long u64;

u8 c17_exchange_byte(_Atomic(u8) *ptr, u8 value) {
  return __c11_atomic_exchange(ptr, value, __ATOMIC_RELAXED);
}

u64 gnu_fetch_add_word(u64 *ptr, u64 value) {
  return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
}

_Bool sync_compare_word(u64 *ptr, u64 expected, u64 desired) {
  return __sync_bool_compare_and_swap(ptr, expected, desired);
}

void c17_system_fence(void) {
  __c11_atomic_thread_fence(__ATOMIC_SEQ_CST);
}
