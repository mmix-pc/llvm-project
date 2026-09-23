// RUN: %clang -### --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -c %S/../../CodeGen/mmix-abi-backend-integration.c -o %t.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=JOB \
// RUN:       --implicit-check-not=-cc1as --implicit-check-not=-isysroot \
// RUN:       --implicit-check-not=-internal-isystem \
// RUN:       --implicit-check-not=-internal-externc-isystem \
// RUN:       --implicit-check-not=crt --implicit-check-not=libclang_rt \
// RUN:       --implicit-check-not=qemu \
// RUN:       --implicit-check-not='{{[/\\](as|ld|gcc)[^/\\"]*"}}'

// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu2x \
// RUN:   -c %S/../../CodeGen/mmix-abi-backend-integration.c -o %t.o
// RUN: llvm-readobj --file-headers --sections --symbols --relocations \
// RUN:   --expand-relocs %t.o | FileCheck %s --check-prefix=ELF
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=DIS --implicit-check-not='<unknown>'

// JOB: (in-process)
// JOB-NEXT: {{.*}}clang{{.*}} "-cc1" "-triple" "mmix-unknown-unknown"
// JOB-SAME: "-emit-obj"
// JOB-SAME: "-mrelocation-model" "static"
// JOB-SAME: "-ffreestanding"
// JOB-SAME: "-std=gnu2x"
// JOB-SAME: "-o" "{{.*}}.o"
// JOB-SAME: "-x" "c" "{{.*}}mmix-abi-backend-integration.c"

// ELF: Format: elf64-mmix
// ELF-NEXT: Arch: mmix
// ELF-NEXT: AddressSize: 64bit
// ELF: Class: 64-bit
// ELF: DataEncoding: BigEndian
// ELF: Type: Relocatable
// ELF: Machine: EM_MMIX
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
// ELF-NEXT: Type: Function
// ELF: Name: external_scalar
// ELF: Section: Undefined
// ELF: Name: global_pointer
// ELF: Type: Object
// ELF: Section: .data
// ELF: Name: global_seed
// ELF: Type: Object
// ELF: Section: .data
// ELF: Name: memcpy
// ELF: Section: Undefined

// DIS-LABEL: <compose>:
// DIS: PUSHJ r31,
// DIS: PUSHGO r31, r{{[0-9]+}}, 0
// DIS: GETA {{r[0-9]+}}, 0
// DIS-NEXT: {{.*}} R_MMIX_GETA external_scalar
// DIS: PUSHGO r31, {{r[0-9]+}}, 0
// DIS: GETA {{r[0-9]+}}, 0
// DIS-NEXT: {{.*}} R_MMIX_GETA external_direct
// DIS: PUSHGO r31, {{r[0-9]+}}, 0
// DIS: GETA {{r[0-9]+}}, 0
// DIS-NEXT: {{.*}} R_MMIX_GETA external_variadic
// DIS: PUSHGO r31, {{r[0-9]+}}, 0
// DIS: GETA r{{[0-9]+}}, 0
// DIS-NEXT: {{.*}} R_MMIX_GETA global_pointer

// DIS-LABEL: <recursive_sum>:
// DIS: GETA r{{[0-9]+}}, 0
// DIS-NEXT: {{.*}} R_MMIX_GETA global_seed
// DIS: PUSHJB r31,

// DIS-LABEL: <copy_block>:
// DIS: GETA {{r[0-9]+}}, 0
// DIS-NEXT: {{.*}} R_MMIX_GETA memcpy
// DIS: PUSHGO r31, {{r[0-9]+}}, 0
