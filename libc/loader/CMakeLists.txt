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

include(LLVMLibCLibraryRules)
set(loader_deps)
foreach(root IN ITEMS libc.loader.linux.mmix.entry
    libc.loader.linux.mmix.bootstrap libc.loader.linux.mmix.continuation)
  collect_object_file_deps(${root} deps)
  list(APPEND loader_deps ${deps})
endforeach()
list(REMOVE_DUPLICATES loader_deps)
set(loader_objects)
foreach(dep IN LISTS loader_deps)
  get_target_property(objects ${dep} OBJECT_FILES)
  list(APPEND loader_objects ${objects})
endforeach()

# Link with explicit ELF policy and no Driver-selected CRT, libc or runtimes.
# The installed path is target-relative; it is never a host library search path.
set(loader_image "${CMAKE_CURRENT_BINARY_DIR}/ld-llvm-mmix.so.1")
add_custom_command(OUTPUT "${loader_image}"
  COMMAND "${CMAKE_LINKER}" -m elf64mmix_linux -shared -e _start
    -z now -z relro --hash-style=both --no-undefined
    -o "${loader_image}" ${loader_objects}
  DEPENDS ${loader_deps} ${loader_objects}
  COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(libc-loader DEPENDS "${loader_image}")
install(PROGRAMS "${loader_image}" DESTINATION lib COMPONENT libc-loader
  EXCLUDE_FROM_ALL)
