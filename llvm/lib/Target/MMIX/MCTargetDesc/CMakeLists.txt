add_llvm_component_library(LLVMMMIXDesc
  MMIXALAsmStreamer.cpp
  MMIXALDependencyGraph.cpp
  MMIXALExpression.cpp
  MMIXALLayout.cpp
  MMIXALStartup.cpp
  MMIXALSymbolMapper.cpp
  MMIXALSymbolTable.cpp
  MMIXAsmBackend.cpp
  MMIXELFObjectWriter.cpp
  MMIXMCCodeEmitter.cpp
  MMIXMCExpr.cpp
  MMIXMCAsmInfo.cpp
  MMIXInstPrinter.cpp
  MMIXALInstPrinter.cpp
  MMIXMCTargetDesc.cpp
  MMIXTargetStreamer.cpp

  LINK_COMPONENTS
  MC
  MMIXInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  MMIX
  )
