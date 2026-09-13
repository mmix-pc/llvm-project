cmake_minimum_required(VERSION 3.20)
get_filename_component(source "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(inputs ${CMAKE_CURRENT_LIST_DIR})
file(MAKE_DIRECTORY ${ROOT})
execute_process(COMMAND ${CLANG} --no-default-config -print-resource-dir
                OUTPUT_VARIABLE resource OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT EXISTS ${resource}/include/stdint.h)
  message(FATAL_ERROR "missing compiler resource headers")
endif()

function(run)
  message(STATUS "${ARGV}")
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "command failed: ${status}: ${ARGV}")
  endif()
endfunction()

# Reuse hdrgen just as libc-public-headers.test does. These are platform-neutral
# declarations from LLVM libc, not a Generic sysroot reused for Linux.
foreach(header assert ctype endian inttypes limits locale math stdint stdio stdlib string strings wchar)
  run(${PYTHON} ${source}/libc/utils/hdrgen/main.py
      -o ${ROOT}/include/${header}.h --write-if-changed
      ${source}/libc/include/${header}.yaml)
endforeach()
configure_file(${inputs}/int128-library-sys-types.h
               ${ROOT}/include/sys/types.h COPYONLY)
configure_file(${inputs}/int128-library-errno.h
               ${ROOT}/include/errno.h COPYONLY)
configure_file(${inputs}/int128-library-endian.h
               ${ROOT}/include/machine/endian.h COPYONLY)

# A bounded, static, single-threaded header profile; int128 is deliberately
# left to upstream compiler feature detection, not set by this configuration.
set(_LIBCPP_ABI_VERSION 1)
set(_LIBCPP_ABI_NAMESPACE __1)
set(_LIBCPP_LIBC_LLVM_LIBC 1)
set(_LIBCPP_PSTL_BACKEND_SERIAL 1)
set(_LIBCPP_HARDENING_MODE_DEFAULT 2)
set(_LIBCPP_ASSERTION_SEMANTIC_DEFAULT 2)
configure_file(${source}/libcxx/include/__config_site.in
               ${ROOT}/include/__config_site @ONLY)
configure_file(${source}/libcxx/vendor/llvm/default_assertion_handler.in
               ${ROOT}/include/__assertion_handler COPYONLY)

foreach(triple mmix-unknown-linux mmix-unknown-unknown)
  set(config ${ROOT}/${triple}/include)
  set(LLVM_DEFAULT_TARGET_TRIPLE ${triple})
  set(LLVM_HOST_TRIPLE ${triple})
  foreach(header llvm-config abi-breaking)
    configure_file(${source}/llvm/include/llvm/Config/${header}.h.cmake
                   ${config}/llvm/Config/${header}.h)
  endforeach()
  set(flags --no-default-config --target=${triple} -std=c++17 -ffreestanding -nostdinc
      -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections
      -DLLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING=1
      -DLIBC_NAMESPACE=__llvm_libc -DLIBC_FULL_BUILD
      -I${config} -I${source}/libcxx/include -I${ROOT}/include
      -I${source}/libc/include -isystem ${resource}/include
      -I${source}/libc -I${source}/llvm/include)
  foreach(level 0 2)
    set(output ${ROOT}/${triple}/O${level})
    file(MAKE_DIRECTORY ${output})
    foreach(unit libc libcxx llvm)
      run(${CLANG} ${flags} -O${level} -c
          ${inputs}/int128-library-${unit}.cpp -o ${output}/${unit}.o)
    endforeach()
    foreach(unit ScaledNumber xxhash)
      run(${CLANG} ${flags} -O${level} -c
          ${source}/llvm/lib/Support/${unit}.cpp -o ${output}/${unit}.o)
    endforeach()
    if(DEFINED HELPERS)
      set(archive ${HELPERS}/lib/${triple}/libclang_rt.builtins.a)
      if(NOT EXISTS ${archive} OR NOT DEFINED LLD OR NOT DEFINED NM)
        message(FATAL_ERROR "scoped links require matching helper archive, LLD and NM")
      endif()
      # Keep only each named arithmetic entry and its dependencies. Full Support
      # objects are compiled above, but their assertions/formatting/memory
      # providers are outside this helper-only link's scope.
      foreach(entry libc_product libcxx_next llvm_saturate llvm_bits)
        run(${LLD} -static --gc-sections -e ${entry}
            ${output}/libc.o ${output}/libcxx.o ${output}/llvm.o
            ${archive} -o ${output}/${entry}.elf)
        execute_process(COMMAND ${NM} --undefined-only ${output}/${entry}.elf
                        RESULT_VARIABLE status OUTPUT_VARIABLE undefined)
        if(NOT status EQUAL 0 OR NOT undefined STREQUAL "")
          message(FATAL_ERROR "unresolved ${entry}: ${undefined}")
        endif()
      endforeach()
    endif()
  endforeach()
endforeach()
