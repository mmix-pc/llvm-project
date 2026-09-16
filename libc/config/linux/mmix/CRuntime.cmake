option(LIBC_MMIX_BUILD_C_RUNTIME "Compose the available Linux C runtime inputs" OFF)
if(NOT LIBC_MMIX_BUILD_C_RUNTIME)
  return()
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/caches/mmix-linux.cmake)

if(NOT CMAKE_CROSSCOMPILING OR NOT LLVM_LIBC_FULL_BUILD OR
   NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "mmix" OR
   NOT LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" OR
   NOT LIBC_TARGET_OS STREQUAL "linux" OR
   NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR LLVM_LIBC_INCLUDE_SCUDO OR
   NOT LIBC_CONF_ERRNO_MODE STREQUAL "LIBC_ERRNO_MODE_SHARED" OR
   NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_SINGLE" OR
   LIBC_CONF_TIMEOUT_ENSURE_MONOTONICITY)
  message(FATAL_ERROR "MMIX C runtime requires cross Linux Release, full libc, SINGLE/shared state and no Scudo")
endif()
foreach(language C CXX ASM)
  if(NOT CMAKE_${language}_COMPILER_TARGET STREQUAL "mmix-unknown-linux")
    message(FATAL_ERROR "MMIX C runtime requires Linux compiler targets")
  endif()
endforeach()
foreach(kind INCLUDE LIBRARY PACKAGE)
  if(NOT CMAKE_FIND_ROOT_PATH_MODE_${kind} STREQUAL "ONLY")
    message(FATAL_ERROR "MMIX C runtime requires sysroot-only ${kind} searches")
  endif()
endforeach()
foreach(component CORE DESCRIPTORS STDIO FILESYSTEM VM TIME PROCESS_BASE
                  RUNTIME_STATE ALLOCATION)
  if(NOT LIBC_MMIX_BUILD_${component})
    message(FATAL_ERROR "MMIX C runtime requires ${component}")
  endif()
endforeach()

# The compiler-rt producer remains independent. Require explicit Linux resources
# instead of searching a host installation or falling back to Generic resources.
set(LIBC_MMIX_COMPILER_RT_CRTBEGIN "" CACHE FILEPATH "Matching Linux compiler-rt crtbegin object")
set(LIBC_MMIX_COMPILER_RT_CRTEND "" CACHE FILEPATH "Matching Linux compiler-rt crtend object")
set(runtime_files "")
get_filename_component(tool_directory "${CMAKE_C_COMPILER}" DIRECTORY)
set(readobj "${tool_directory}/llvm-readobj")
if(NOT EXISTS "${readobj}")
  message(FATAL_ERROR "MMIX C runtime requires llvm-readobj beside Clang")
endif()
foreach(provider BUILTINS CRTBEGIN CRTEND)
  set(path "${LIBC_MMIX_COMPILER_RT_${provider}}")
  if(NOT IS_ABSOLUTE "${path}" OR NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
    message(FATAL_ERROR "MMIX C runtime requires an explicit ${provider} file")
  endif()
  get_filename_component(path "${path}" REALPATH)
  get_filename_component(directory "${path}" DIRECTORY)
  get_filename_component(triple "${directory}" NAME)
  if(NOT triple STREQUAL "mmix-unknown-linux")
    message(FATAL_ERROR "MMIX C runtime requires compiler-rt resources in mmix-unknown-linux")
  endif()
  if(path IN_LIST runtime_files)
    message(FATAL_ERROR "MMIX C runtime requires distinct compiler-rt providers")
  endif()
  execute_process(COMMAND "${readobj}" --file-headers --sections "${path}"
    RESULT_VARIABLE result OUTPUT_VARIABLE headers ERROR_VARIABLE error)
  string(REGEX MATCHALL "Format: [^\n]+" formats "${headers}")
  if(NOT result EQUAL 0 OR NOT formats OR
     headers MATCHES "SHF_TLS|[.]MMIX[.]reg_contents")
    message(FATAL_ERROR "MMIX C runtime requires non-TLS Linux ELF providers: ${path}: ${error}")
  endif()
  foreach(format IN LISTS formats)
    if(NOT format STREQUAL "Format: elf64-mmix")
      message(FATAL_ERROR "MMIX C runtime rejects non-MMIX provider ${path}")
    endif()
  endforeach()
  list(APPEND runtime_files "${path}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${runtime_files})

# Preparation archives stay separate: consumers use normal archive extraction,
# not --whole-archive, so shared object dependencies have one definition.
add_custom_target(mmix_libc_c_runtime
  DEPENDS libc libc-startup libc-headers mmix_libc_allocation
    ${runtime_files})
# Describe provider selection, not runtime qualification or execution evidence.
# FIXME: Replace this transitional manual list with complete provider accounting.
set(mmix_runtime_excluded "fork;spawn;signals;pthread;TLS;dynamic-linking")
if(LIBC_MMIX_BUILD_FORK)
  list(REMOVE_ITEM mmix_runtime_excluded fork)
endif()
if(LIBC_MMIX_BUILD_SIGNAL_MASKS)
  list(REMOVE_ITEM mmix_runtime_excluded signals)
endif()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/mmix-c-runtime/Inputs.cmake" CONTENT
"set(MMIX_C_RUNTIME_LIBC \"$<TARGET_FILE:libc>\")
set(MMIX_C_RUNTIME_STATE \"$<TARGET_FILE:mmix_libc_state>\")
set(MMIX_C_RUNTIME_CRT1 \"$<TARGET_OBJECTS:libc.startup.linux.mmix.crt1>\")
set(MMIX_C_RUNTIME_BUILTINS \"${LIBC_MMIX_COMPILER_RT_BUILTINS}\")
set(MMIX_C_RUNTIME_CRTBEGIN \"${LIBC_MMIX_COMPILER_RT_CRTBEGIN}\")
set(MMIX_C_RUNTIME_CRTEND \"${LIBC_MMIX_COMPILER_RT_CRTEND}\")
set(MMIX_C_RUNTIME_HEADERS \"${LIBC_INCLUDE_DIR}\")
set(MMIX_C_RUNTIME_ENTRYPOINTS \"${TARGET_LIBC_ENTRYPOINTS}\")
set(MMIX_C_RUNTIME_SYSROOT \"${CMAKE_SYSROOT}\")
set(MMIX_C_RUNTIME_KERNEL_HEADERS \"${LIBC_KERNEL_HEADERS}\")
set(MMIX_C_RUNTIME_COMPILER \"${CMAKE_CXX_COMPILER}\")
set(MMIX_C_RUNTIME_SOURCE \"${LIBC_SOURCE_DIR}\")
set(MMIX_C_RUNTIME_NAMESPACE \"${LIBC_NAMESPACE}\")
# Future publication destinations; this target does not install resources.
set(MMIX_C_RUNTIME_HEADER_DESTINATION \"usr/include\")
set(MMIX_C_RUNTIME_LIBRARY_DESTINATION \"usr/lib\")
set(MMIX_C_RUNTIME_RESOURCE_DESTINATION \"lib/mmix-unknown-linux\")
set(MMIX_C_RUNTIME_EXCLUDED \"${mmix_runtime_excluded}\")
")
