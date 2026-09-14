# Explicit build-tree composition, not an installed or link-complete runtime.
option(LIBC_MMIX_BUILD_ALLOCATION "Prepare Linux allocation consumers and inputs" OFF)
if(NOT LIBC_MMIX_BUILD_ALLOCATION)
  return()
endif()
if(NOT LIBC_MMIX_BUILD_RUNTIME_STATE OR LLVM_LIBC_INCLUDE_SCUDO)
  message(FATAL_ERROR "MMIX Linux allocation preparation requires runtime state without Scudo")
endif()
set(LIBC_MMIX_COMPILER_RT_BUILTINS "" CACHE FILEPATH "Matching Linux compiler-rt builtins archive")
if(NOT IS_ABSOLUTE "${LIBC_MMIX_COMPILER_RT_BUILTINS}" OR
   NOT EXISTS "${LIBC_MMIX_COMPILER_RT_BUILTINS}" OR
   IS_DIRECTORY "${LIBC_MMIX_COMPILER_RT_BUILTINS}")
  message(FATAL_ERROR "MMIX Linux allocation requires an explicit compiler-rt archive")
endif()
add_custom_target(mmix_libc_allocation
  DEPENDS libc mmix_libc_state libc.src.__support.File.file
    libc.src.__support.OSUtil.linux.mmix.syscall_asm
    "${LIBC_MMIX_COMPILER_RT_BUILTINS}")
# Keep providers separate so consumers can audit extraction and ownership.
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/mmix-allocation/Inputs.cmake" CONTENT
"set(MMIX_ALLOCATION_LIBC \"$<TARGET_FILE:libc>\")
set(MMIX_ALLOCATION_STATE \"$<TARGET_FILE:mmix_libc_state>\")
set(MMIX_ALLOCATION_SYSCALL \"$<TARGET_OBJECTS:libc.src.__support.OSUtil.linux.mmix.syscall_asm>\")
set(MMIX_ALLOCATION_FILE \"$<TARGET_OBJECTS:libc.src.__support.File.file>\")
set(MMIX_ALLOCATION_BUILTINS \"${LIBC_MMIX_COMPILER_RT_BUILTINS}\")
set(MMIX_ALLOCATION_HEADERS \"${LIBC_INCLUDE_DIR}\")
set(MMIX_ALLOCATION_SYSROOT \"${CMAKE_SYSROOT}\")
set(MMIX_ALLOCATION_KERNEL_HEADERS \"${LIBC_KERNEL_HEADERS}\")
set(MMIX_ALLOCATION_NAMESPACE \"${LIBC_NAMESPACE}\")
set(MMIX_ALLOCATION_SOURCE \"${LIBC_SOURCE_DIR}\")
set(MMIX_ALLOCATION_COMPILER \"${CMAKE_CXX_COMPILER}\")
")
