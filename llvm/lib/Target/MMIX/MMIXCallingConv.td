//===-- MMIXCallingConv.td - MMIX calling conventions ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Local registers are preserved by PUSHJ/PUSHGO and POP rather than by
// ordinary memory spills. The frame pointer is saved only when a function uses
// it. Stack and register-stack state is preserved without appearing in the
// software callee-save list.
def CC_MMIX : CallingConv<[
  CCIfSRet<CCIfType<[i64], CCAssignToReg<[R251]>>>,
  CCIfType<[i1, i8, i16, i32], CCPromoteToType<i64>>,
  CCIfType<[f32], CCBitConvertToType<i64>>,
  CCIfType<[i64, f64],
      CCAssignToReg<[R231, R232, R233, R234, R235, R236, R237, R238,
                     R239, R240, R241, R242, R243, R244, R245, R246]>>,
  CCIfType<[i64, f64], CCAssignToStack<8, 8>>
]>;

def RetCC_MMIX : CallingConv<[
  CCIfType<[i1, i8, i16, i32], CCPromoteToType<i64>>,
  CCIfType<[f32], CCBitConvertToType<i64>>,
  CCIfType<[i64, f64], CCAssignToReg<[R231, R232]>>
]>;

def CSR_MMIX : CalleeSavedRegs<
    (add (sequence "R%u", 0, 30), R253)> {
  let OtherPreserved = (add R254, RG, RL, RO);
}

def CSR_MMIX_Linux : CalleeSavedRegs<
    (add (sequence "R%u", 0, 30), R253)> {
  let OtherPreserved = (add R230, R254, RG, RL, RO);
}
