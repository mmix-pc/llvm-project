; RUN: split-file %s %t
; RUN: llvm-as %t/external.ll -o %t/external.bc
; RUN: llvm-as %t/initialexec.ll -o %t/initialexec.bc
; RUN: llvm-as %t/localdynamic.ll -o %t/localdynamic.bc
; RUN: llvm-as %t/refined.ll -o %t/refined.bc
; RUN: llvm-lto2 run -relocation-model=static %t/external.bc -o %t/external-O0 -O0 -r=%t/external.bc,address,plx -r=%t/external.bc,x,
; RUN: llvm-objdump -dr %t/external-O0.0 | FileCheck %s --check-prefix=IE --implicit-check-not=__tls_get_addr --implicit-check-not=__emutls
; RUN: llvm-lto2 run -relocation-model=static %t/external.bc -o %t/external-O2 -O2 -r=%t/external.bc,address,plx -r=%t/external.bc,x,
; RUN: llvm-objdump -dr %t/external-O2.0 | FileCheck %s --check-prefix=IE --implicit-check-not=__tls_get_addr --implicit-check-not=__emutls
; RUN: llvm-lto2 run -relocation-model=static %t/initialexec.bc -o %t/initialexec -O2 -r=%t/initialexec.bc,address,plx -r=%t/initialexec.bc,x,
; RUN: llvm-objdump -dr %t/initialexec.0 | FileCheck %s --check-prefix=IE --implicit-check-not=__tls_get_addr --implicit-check-not=__emutls
; RUN: llvm-lto2 run -relocation-model=static %t/localdynamic.bc -o %t/localdynamic -O2 -r=%t/localdynamic.bc,address,plx -r=%t/localdynamic.bc,x,
; RUN: llvm-objdump -dr %t/localdynamic.0 | FileCheck %s --check-prefix=IE --implicit-check-not=__tls_get_addr --implicit-check-not=__emutls
; RUN: llvm-lto2 run -relocation-model=static %t/refined.bc -o %t/refined -O2 -r=%t/refined.bc,address,plx -r=%t/refined.bc,x,plx
; RUN: llvm-objdump -dr %t/refined.0 | FileCheck %s --check-prefix=REFINED --implicit-check-not=__tls_get_addr --implicit-check-not=__emutls

; Keep the unresolved declarations non-DSO-local in the LTO resolution. A
; static relocation model refines these declarations to initial-exec, while
; resolved executable definitions can use local-exec without a GOT slot.
; IE-LABEL: <address>:
; IE: R_MMIX_GOTTPREL_PC64 x
; IE: r230
; IE: ADDU r231,
; REFINED-LABEL: <address>:
; REFINED: R_MMIX_TPREL_LO16 x
; REFINED: R_MMIX_TPREL_ML16 x
; REFINED: R_MMIX_TPREL_MH16 x
; REFINED: R_MMIX_TPREL_HI16 x
; REFINED: ADDU r231, r230, r231

;--- external.ll
target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
target triple = "mmix-unknown-linux"
@x = external thread_local global i64
define ptr @address() { ret ptr @x }

;--- initialexec.ll
target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
target triple = "mmix-unknown-linux"
@x = external thread_local(initialexec) global i64
define ptr @address() { ret ptr @x }

;--- localdynamic.ll
target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
target triple = "mmix-unknown-linux"
@x = external thread_local(localdynamic) global i64
define ptr @address() { ret ptr @x }

;--- refined.ll
target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
target triple = "mmix-unknown-linux"
@x = dso_local thread_local(localdynamic) global i64 7
define ptr @address() { ret ptr @x }
