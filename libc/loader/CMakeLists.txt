option(LIBC_BUILD_LOADER "Build the experimental Linux ELF loader components" OFF)
if(NOT LIBC_BUILD_LOADER)
  return()
endif()

if(NOT LIBC_TARGET_OS_IS_LINUX OR
   NOT LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  message(FATAL_ERROR "The libc loader currently requires MMIX Linux")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "The libc loader requires Clang")
endif()

include(CompileOptions.cmake)
add_subdirectory(linux)

# Entry and relocation support must exist before producing or installing
# ld-llvm-mmix.so.1. Do not publish a placeholder interpreter.
add_custom_target(libc-loader DEPENDS
  libc.loader.linux.mmix.target libc.loader.linux.io libc.loader.linux.arena
  libc.loader.linux.elf libc.loader.linux.entry libc.loader.linux.mmix.entry
  libc.loader.linux.mmix.bootstrap libc.loader.linux.self_relocation
  libc.loader.linux.dynamic libc.loader.linux.mapping libc.loader.linux.search
  libc.loader.linux.graph libc.loader.linux.symbols libc.loader.linux.relocation
  libc.loader.linux.protection)
