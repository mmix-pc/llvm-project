//===-- Freestanding loader handoff probe ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

.text
.p2align 2
.globl _start
.type _start,@function
.hidden loader_check_handoff
_start:
  // Observe entry state before creating local registers or a call window.
  GET r231,rL
  BNZ r231,.Lfail
  GET r231,rG
  CMPU r231,r231,230
  BNZ r231,.Lfail
  BNZ r230,.Lfail
  GET r231,rO
  GET r232,rS
  CMPU r231,r231,r232
  BNZ r231,.Lfail
  AND r231,r254,7
  BNZ r231,.Lfail
  OR r0,r254,0
  OR r231,r254,0
  PUSHJ r31,loader_check_handoff
  CMPU r232,r254,r0
  BNZ r232,.Lfail
  JMP .Lexit
.Lfail:
  SETL r231,1
.Lexit:
  SETL r237,94 // Linux exit_group.
  TRAP 1,0,0
  TRAP 255,0,0
.size _start,.-_start
.section .note.GNU-stack,"",@progbits
