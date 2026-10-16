include_guard(GLOBAL)

# Do not reuse a static-provider validation result for PIC runtime composition.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "mmix" OR NOT CMAKE_CROSSCOMPILING OR
   NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR
   NOT CMAKE_POSITION_INDEPENDENT_CODE OR
   LIBCXX_MMIX_LINUX_INSTALL_RUNTIME)
  message(FATAL_ERROR "MMIX resident preparation requires a non-installed PIC Linux Release build")
endif()
foreach(project LIBCXX LIBCXXABI LIBUNWIND)
  if(${project}_INSTALL_HEADERS OR ${project}_INSTALL_LIBRARY)
    message(FATAL_ERROR "MMIX resident preparation does not install runtimes")
  endif()
  if(${project}_ENABLE_SHARED OR NOT ${project}_ENABLE_STATIC)
    message(FATAL_ERROR "MMIX resident preparation composes PIC archives explicitly")
  endif()
endforeach()
foreach(option LIBCXXABI_ENABLE_THREADS LIBCXXABI_HAS_PTHREAD_API
               LIBCXXABI_ENABLE_EXCEPTIONS LIBCXXABI_ENABLE_RTTI
               LIBUNWIND_ENABLE_THREADS LIBUNWIND_IS_NATIVE_ONLY
               LIBUNWIND_REMEMBER_HEAP_ALLOC)
  if(NOT ${option})
    message(FATAL_ERROR "MMIX resident preparation requires ${option}=ON")
  endif()
endforeach()
foreach(option LIBCXX_HAS_EXTERNAL_THREAD_API LIBCXXABI_HAS_EXTERNAL_THREAD_API
               LIBUNWIND_USE_FRAME_HEADER_CACHE LIBUNWIND_ENABLE_FRAME_APIS
               LIBUNWIND_IS_BAREMETAL LIBUNWIND_ENABLE_ASSERTIONS
               LIBUNWIND_HIDE_SYMBOLS LIBCXXABI_HERMETIC_STATIC_LIBRARY)
  if(${option})
    message(FATAL_ERROR "MMIX resident preparation rejects ${option}")
  endif()
endforeach()
foreach(flag -D_LIBUNWIND_MMIX_LINUX_THREADS -D_LIBUNWIND_MMIX_STARTUP_IMAGES)
  if(NOT flag IN_LIST LIBUNWIND_ADDITIONAL_COMPILE_FLAGS)
    message(FATAL_ERROR "MMIX resident preparation requires ${flag}")
  endif()
endforeach()
foreach(language C CXX ASM)
  if(NOT CMAKE_${language}_COMPILER_TARGET STREQUAL "mmix-unknown-linux")
    message(FATAL_ERROR "MMIX resident preparation requires Linux compiler targets")
  endif()
endforeach()
foreach(language C CXX)
  separate_arguments(flags NATIVE_COMMAND "${CMAKE_${language}_FLAGS}")
  foreach(flag -fPIC -ftls-model=initial-exec -fno-lto)
    if(NOT flag IN_LIST flags)
      message(FATAL_ERROR "MMIX resident preparation requires ${flag}")
    endif()
  endforeach()
  foreach(flag IN LISTS flags)
    if(flag MATCHES "^-ftls-model=" AND NOT flag STREQUAL "-ftls-model=initial-exec" OR
       flag MATCHES "^-flto|^-f(no-pic|no-PIC|emulated-tls)")
      message(FATAL_ERROR "MMIX resident preparation rejects conflicting ${language} flags")
    endif()
  endforeach()
endforeach()
set(libc_build "${LIBCXX_MMIX_LINUX_LIBC_BUILD}")
set(rt_build "${LIBCXX_MMIX_LINUX_COMPILER_RT_BUILD}")
foreach(root libc_build rt_build)
  if(NOT IS_ABSOLUTE "${${root}}" OR NOT EXISTS "${${root}}/CMakeCache.txt")
    message(FATAL_ERROR "MMIX resident preparation requires explicit provider builds")
  endif()
endforeach()
load_cache("${libc_build}" READ_WITH_PREFIX INPUT_
  LIBC_MMIX_BUILD_SHARED_LIBC CMAKE_SYSROOT CMAKE_C_COMPILER CMAKE_HOME_DIRECTORY)
file(REAL_PATH "${CMAKE_SYSROOT}" sysroot)
file(REAL_PATH "${INPUT_CMAKE_SYSROOT}" input_sysroot)
file(REAL_PATH "${CMAKE_C_COMPILER}" compiler)
file(REAL_PATH "${INPUT_CMAKE_C_COMPILER}" input_compiler)
if(NOT INPUT_LIBC_MMIX_BUILD_SHARED_LIBC OR NOT sysroot STREQUAL input_sysroot OR
   NOT compiler STREQUAL input_compiler)
  message(FATAL_ERROR "MMIX resident preparation requires matching shared libc inputs")
endif()
file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../../runtimes" expected_source)
file(REAL_PATH "${INPUT_CMAKE_HOME_DIRECTORY}" input_source)
if(NOT expected_source STREQUAL input_source)
  message(FATAL_ERROR "MMIX resident preparation requires the same libc source")
endif()
load_cache("${rt_build}" READ_WITH_PREFIX RT_ CMAKE_C_COMPILER
  CMAKE_C_COMPILER_TARGET CMAKE_HOME_DIRECTORY CMAKE_C_FLAGS)
file(REAL_PATH "${RT_CMAKE_C_COMPILER}" rt_compiler)
file(REAL_PATH "${RT_CMAKE_HOME_DIRECTORY}" rt_source)
file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../../compiler-rt" expected_source)
separate_arguments(rt_flags NATIVE_COMMAND "${RT_CMAKE_C_FLAGS}")
if(NOT compiler STREQUAL rt_compiler OR NOT expected_source STREQUAL rt_source OR
   NOT RT_CMAKE_C_COMPILER_TARGET STREQUAL "mmix-unknown-linux" OR
   NOT "-fPIC" IN_LIST rt_flags)
  message(FATAL_ERROR "MMIX resident preparation requires matching PIC builtins")
endif()
foreach(language CXX ASM)
  file(REAL_PATH "${CMAKE_${language}_COMPILER}" selected)
  if(NOT selected STREQUAL compiler)
    message(FATAL_ERROR "MMIX resident preparation requires one compiler")
  endif()
endforeach()
set(libc_image "${libc_build}/mmix-shared-libc/libc.so.1")
set(builtins "${rt_build}/lib/mmix-unknown-linux/libclang_rt.builtins.a")
foreach(path "${libc_image}" "${builtins}" "${libc_build}/libc/include/pthread.h")
  if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
    message(FATAL_ERROR "Missing resident provider input: ${path}")
  endif()
endforeach()
foreach(language C CXX)
  string(APPEND CMAKE_${language}_FLAGS
    " -nostdlibinc -isystem \"${libc_build}/libc/include\" -idirafter=/usr/include")
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/InstalledLibc.cmake")
if(NOT RUNTIMES_USE_LIBC STREQUAL "llvm-libc")
  message(FATAL_ERROR "MMIX resident preparation requires LLVM libc")
endif()
set(LIBCXX_BUILTINS_LIBRARY "${builtins}")
set(LIBCXXABI_BUILTINS_LIBRARY "${builtins}")
set(MMIX_LINUX_RESIDENT_RUNTIME_VALIDATED TRUE)

function(mmix_resident_providers)
  target_sources(unwind_static_objects PRIVATE
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../../libunwind/src/mmix/StartupImages.cpp"
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../../libunwind/src/mmix/CursorLifetime.cpp")
  set(out "${CMAKE_BINARY_DIR}/mmix-resident-runtime")
  file(GENERATE OUTPUT "${out}/unwind.map" CONTENT
    "{ global: _Unwind_*; unw_*; __gcc_personality_v0; __libunwind_mmix_adopt_startup_images; __libunwind_mmix_cursor_begin; __libunwind_mmix_cursor_end; __libunwind_mmix_cursor_finish; __libunwind_mmix_set_managed_stop; local: *; };\n")
  file(GENERATE OUTPUT "${out}/cleanup.map" CONTENT
    "{ global: __cxa_begin_catch; __gxx_personality_v0; _ZSt9terminatev; local: *; };\n")
  add_custom_command(OUTPUT "${out}/libunwind.so.1"
    COMMAND ${CMAKE_LINKER} -m elf64mmix_linux -shared -soname libunwind.so.1
      -z now -z relro --hash-style=both --eh-frame-hdr -z defs
      --allow-shlib-undefined --version-script=${out}/unwind.map
      --whole-archive $<TARGET_FILE:unwind_static> --no-whole-archive
      "${builtins}" "${libc_image}" -o "${out}/libunwind.so.1"
    DEPENDS unwind_static "${builtins}" "${libc_image}" "${out}/unwind.map"
    VERBATIM)
  # Extract the real cleanup closure, not the complete public C++ ABI. No
  # replacement personality, terminate handler or duplicate libc state.
  add_custom_command(OUTPUT "${out}/libc++abi.so.1"
    COMMAND ${CMAKE_LINKER} -m elf64mmix_linux -shared -soname libc++abi.so.1
      -z now -z relro --hash-style=both --eh-frame-hdr -z defs
      --allow-shlib-undefined --version-script=${out}/cleanup.map
      -u __gxx_personality_v0 -u __cxa_begin_catch -u _ZSt9terminatev
      $<TARGET_FILE:cxxabi_static> "${out}/libunwind.so.1"
      "${builtins}" "${libc_image}" -o "${out}/libc++abi.so.1"
    DEPENDS cxxabi_static "${builtins}" "${libc_image}"
      "${out}/libunwind.so.1" "${out}/cleanup.map" VERBATIM)
  include("${libc_build}/mmix-shared-libc/link-inputs.cmake")
  if(NOT MMIX_SHARED_LIBC_BUILTINS STREQUAL builtins)
    message(FATAL_ERROR "Resident libc and unwind require the same PIC builtins")
  endif()
  add_custom_command(OUTPUT "${out}/libc.so.1"
    COMMAND ${CMAKE_LINKER} -m elf64mmix_linux -shared -soname libc.so.1
      -z now -z relro --hash-style=both --eh-frame-hdr -z defs
      --allow-shlib-undefined --version-script=${MMIX_SHARED_LIBC_EXPORTS}
      --whole-archive "${MMIX_SHARED_LIBC_ARCHIVE}" --no-whole-archive
      ${MMIX_SHARED_LIBC_OBJECTS} "${builtins}"
      "${out}/libc++abi.so.1" "${out}/libunwind.so.1" -o "${out}/libc.so.1"
    DEPENDS "${MMIX_SHARED_LIBC_ARCHIVE}" ${MMIX_SHARED_LIBC_OBJECTS}
      "${builtins}" "${MMIX_SHARED_LIBC_EXPORTS}"
      "${out}/libunwind.so.1" "${out}/libc++abi.so.1"
    COMMAND_EXPAND_LISTS VERBATIM)
  add_custom_target(mmix_resident_runtime DEPENDS "${out}/libc.so.1")
endfunction()
cmake_language(DEFER CALL mmix_resident_providers)
