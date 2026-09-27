; RUN: split-file %s %t
; RUN: llvm-as %t/external.ll -o %t/external.bc
; RUN: llvm-as %t/initialexec.ll -o %t/initialexec.bc
; RUN: llvm-as %t/localdynamic.ll -o %t/localdynamic.bc
; RUN: llvm-as %t/refined.ll -o %t/refined.bc
; RUN: not llvm-lto2 run -relocation-model=static %t/external.bc -o %t/out -O0 -r=%t/external.bc,address,plx -r=%t/external.bc,x, 2>&1 | FileCheck %s --check-prefix=MODEL
; RUN: not llvm-lto2 run -relocation-model=static %t/external.bc -o %t/out -O2 -r=%t/external.bc,address,plx -r=%t/external.bc,x, 2>&1 | FileCheck %s --check-prefix=MODEL
; RUN: not llvm-lto2 run -relocation-model=static %t/initialexec.bc -o %t/out -O2 -r=%t/initialexec.bc,address,plx -r=%t/initialexec.bc,x, 2>&1 | FileCheck %s --check-prefix=MODEL
; RUN: not llvm-lto2 run -relocation-model=static %t/localdynamic.bc -o %t/out -O2 -r=%t/localdynamic.bc,address,plx -r=%t/localdynamic.bc,x, 2>&1 | FileCheck %s --check-prefix=MODEL
; RUN: llvm-lto2 run -relocation-model=static %t/refined.bc -o %t/refined -O2 -r=%t/refined.bc,address,plx -r=%t/refined.bc,x,plx
; RUN: llvm-objdump -dr %t/refined.0 | FileCheck %s --check-prefix=REFINED --implicit-check-not=__tls_get_addr --implicit-check-not=__emutls

; Keep the unresolved declarations non-DSO-local in the LTO resolution. A
; static executable link can refine resolved definitions to local-exec; that
; must not erase the backend's diagnostic for a remaining initial-exec model.
; MODEL: MMIX Linux supports only the effective local-exec TLS model
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
