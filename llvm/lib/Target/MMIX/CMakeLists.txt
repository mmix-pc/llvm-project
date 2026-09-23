add_llvm_component_group(MMIX)

set(LLVM_TARGET_DEFINITIONS MMIX.td)

tablegen(LLVM MMIXGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM MMIXGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM MMIXGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM MMIXGenCallingConv.inc -gen-callingconv)
tablegen(LLVM MMIXGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM MMIXGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM MMIXGenMMIXALAsmWriter.inc -gen-asm-writer -asmwriternum=1)
tablegen(LLVM MMIXGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM MMIXGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM MMIXGenMCCodeEmitter.inc -gen-emitter)

add_public_tablegen_target(MMIXCommonTableGen)

add_llvm_target(MMIXCodeGen
  MMIXAddressEmission.cpp
  MMIXAggregateABI.cpp
  MMIXALModuleValidator.cpp
  MMIXAsmPrinter.cpp
  MMIXFrameLowering.cpp
  MMIXInstrInfo.cpp
  MMIXISelDAGToDAG.cpp
  MMIXISelLowering.cpp
  MMIXMachineFunctionInfo.cpp
  MMIXRegisterInfo.cpp
  MMIXSelectionDAGInfo.cpp
  MMIXMCInstLower.cpp
  MMIXSubtarget.cpp
  MMIXTailCall.cpp
  MMIXTargetMachine.cpp

  LINK_COMPONENTS
  MMIXAsmParser
  MMIXDisassembler
  MMIXDesc
  MMIXInfo
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target

  ADD_TO_COMPONENT
  MMIX
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(TargetInfo)
