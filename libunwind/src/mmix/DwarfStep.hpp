//===-- MMIX native DWARF stepping ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_MMIX_DWARF_STEP_HPP
#define LIBUNWIND_MMIX_DWARF_STEP_HPP

namespace libunwind {

template <typename A>
int restoreMMIXWindow(A &addressSpace, const Registers_mmix &registers,
                      Registers_mmix &caller) {
  uint64_t window = registers.getRegisterWindow();
  uint64_t global = registers.getRegister(UNW_MMIX_RG);
  if (window < 8 || (window & 7) || global < 32 || global > 231)
    return UNW_EBADFRAME;
  uint64_t count = addressSpace.get64(window - 8);
  if (count > global || window < (count + 1) * 8)
    return UNW_EBADFRAME;
  uint64_t previous = window - (count + 1) * 8;
  for (unsigned index = 0; index < count; ++index) {
    int dwarf = index < 224 ? UNW_MMIX_R0 + index : index - 224;
    caller.setRegister(dwarf, addressSpace.get64(previous + index * 8));
  }
  caller.setRegisterWindow(previous);
  caller.setRegister(UNW_MMIX_RL, count);
  return UNW_STEP_SUCCESS;
}

template <typename A>
int finishMMIXDwarfStep(A &addressSpace, const Registers_mmix &registers,
                        Registers_mmix &caller, bool signalFrame) {
  if (!caller.getIP())
    return UNW_STEP_SUCCESS;
  if (signalFrame)
    return UNW_EINVAL;
  // Both stacks use the original activation; CFI must not see the caller's rO.
  return restoreMMIXWindow(addressSpace, registers, caller);
}

template <typename A>
int stepWithMMIXDwarf(A &addressSpace, uint64_t pc, uint64_t fde,
                      Registers_mmix &registers, bool &isSignalFrame,
                      bool stage2) {
  bool signalFrame = false;
  int result = DwarfInstructions<A, Registers_mmix>::stepWithDwarf(
      addressSpace, pc, fde, registers, signalFrame, stage2,
      finishMMIXDwarfStep<A>);
  if (result == UNW_STEP_SUCCESS && registers.getIP())
    isSignalFrame = false;
  return result;
}

} // namespace libunwind

#endif
