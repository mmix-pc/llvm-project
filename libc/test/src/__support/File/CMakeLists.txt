if(NOT (TARGET libc.src.__support.threads.mutex) OR LIBC_TARGET_OS_IS_GPU)
  # Not all platforms have a mutex implementation. If mutex is unvailable,
  # we just skip everything about files. The GPU does not currently support
  # files as well.
  return()
endif()

add_libc_test(
  file_test
  SUITE
    libc-support-tests
  SRCS
    file_test.cpp
  DEPENDS
    libc.test.src.locale_guard
    libc.include.stdio
    libc.hdr.wchar_macros
    libc.hdr.types.size_t
    libc.src.errno.errno
    libc.src.__support.CPP.new
    libc.src.__support.File.file
    libc.src.__support.error_or
    libc.test.UnitTest.MemoryMatcher
)

add_libc_test(
  platform_file_test
  SUITE
    libc-support-tests
  SRCS
    platform_file_test.cpp
  DEPENDS
    libc.src.__support.File.file
    libc.src.__support.File.platform_file
    libc.include.stdio
)

set(platform_stream_targets "platform_stdout;platform_stdin;platform_stderr")
foreach(target IN LISTS platform_file_targets)
  if(TARGET libc.src.__support.File.${target})
    add_libc_test(
      ${target}_test
      FULL_BUILD_ONLY
      SUITE
        libc-support-tests
      SRCS
        ${target}_test.cpp
      DEPENDS
        libc.src.__support.File.file
        libc.src.__support.File.${target}
        libc.include.stdio
    )
  endif()
endforeach()

add_subdirectory(testdata)

if(LIBC_TARGET_OS_IS_LINUX AND NOT LLVM_LIBC_FULL_BUILD)
  add_libc_test(
    dir_ownership_test
    OVERLAY_BUILD_ONLY
    SUITE libc-support-tests
    SRCS dir_ownership_test.cpp ${LIBC_SOURCE_DIR}/src/__support/File/dir.cpp
    DEPENDS
      libc.src.__support.CPP.new
      libc.src.__support.CPP.mutex
      libc.src.__support.CPP.span
      libc.src.__support.threads.mutex
      libc.src.__support.alloc_checker
      libc.src.__support.error_or
      libc.hdr.types.struct_dirent
      libc.hdr.errno_macros
    LINK_LIBRARIES "-Wl,--wrap=malloc"
  )
endif()

if(LIBC_TARGET_OS_IS_LINUX AND NOT LLVM_LIBC_FULL_BUILD)
  add_libc_test(
    linux_file_allocation_test
    OVERLAY_BUILD_ONLY
    SUITE
      libc-support-tests
    SRCS
      linux_file_allocation_test.cpp
      ${LIBC_SOURCE_DIR}/src/stdio/generic/fclose.cpp
      ${LIBC_SOURCE_DIR}/src/stdio/generic/freopen.cpp
      ${LIBC_SOURCE_DIR}/src/stdio/fmemopen.cpp
      ${LIBC_SOURCE_DIR}/src/stdio/open_memstream.cpp
      ${LIBC_SOURCE_DIR}/src/wchar/open_wmemstream.cpp
    DEPENDS
      libc.src.errno.errno
      libc.hdr.func.free
      libc.hdr.func.malloc
      libc.hdr.func.realloc
      libc.src.__support.CPP.new
      libc.src.__support.File.file
      libc.src.__support.File.memory_file
      libc.src.__support.File.platform_file
      libc.src.__support.alloc_checker
      libc.src.__support.common
      libc.src.string.memory_utils.inline_memcpy
      libc.src.string.memory_utils.inline_memset
    LINK_LIBRARIES
      "-Wl,--wrap=malloc"
      "-Wl,--wrap=free"
  )
endif()
