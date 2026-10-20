add_custom_target(libc_stdio_ext_unittests)

add_libc_test(
  memory_stream_test
  FULL_BUILD_ONLY
  SUITE libc_stdio_ext_unittests
  SRCS memory_stream_test.cpp
  DEPENDS
    libc.hdr.func.free
    libc.src.stdio.fclose
    libc.src.stdio.fflush
    libc.src.stdio.fmemopen
    libc.src.stdio.fread
    libc.src.stdio.fseek
    libc.src.stdio.ftell
    libc.src.stdio.fwrite
    libc.src.stdio.open_memstream
    libc.src.stdio.setvbuf
    libc.src.stdio_ext.__fbufsize
    libc.src.stdio_ext.__fpending
    libc.src.stdio_ext.__fpurge
    libc.src.stdio_ext.__freadable
    libc.src.stdio_ext.__fwritable
)

set(extension_sources "")
foreach(entry __freadable __fwritable __freading __fwriting __flbf __fbufsize
              __fpending __fpurge _flushlbf)
  list(APPEND extension_sources ${LIBC_SOURCE_DIR}/src/stdio_ext/${entry}.cpp)
endforeach()
add_libc_test(
  stream_test
  SUITE libc_stdio_ext_unittests
  SRCS
    stream_test.cpp
    ${extension_sources}
    ${LIBC_SOURCE_DIR}/src/stdio/generic/fread.cpp
    ${LIBC_SOURCE_DIR}/src/stdio/generic/fwrite.cpp
    ${LIBC_SOURCE_DIR}/src/__support/File/file.cpp
  DEPENDS
    libc.src.__support.File.file
    libc.src.__support.CPP.scope
    libc.src.errno.errno
  COMPILE_OPTIONS -ULIBC_COPT_STDIO_USE_SYSTEM_FILE -DFile=ExtensionFile
)

if(LIBC_TARGET_OS_IS_LINUX)
  if(LLVM_LIBC_FULL_BUILD)
    set(thread_deps libc.src.pthread.pthread_create libc.src.pthread.pthread_join)
    set(thread_libs "")
  else()
    set(thread_deps "")
    set(thread_libs pthread)
  endif()
  add_libc_test(
    fsetlocking_test
    SUITE libc_stdio_ext_unittests
    SRCS
      fsetlocking_test.cpp
      ${LIBC_SOURCE_DIR}/src/stdio_ext/__fsetlocking.cpp
      ${LIBC_SOURCE_DIR}/src/stdio_ext/_flushlbf.cpp
      ${LIBC_SOURCE_DIR}/src/stdio/flockfile.cpp
      ${LIBC_SOURCE_DIR}/src/stdio/funlockfile.cpp
      ${LIBC_SOURCE_DIR}/src/stdio/generic/vfprintf.cpp
      ${LIBC_SOURCE_DIR}/src/stdio/generic/vfscanf.cpp
      ${LIBC_SOURCE_DIR}/src/__support/File/file.cpp
    DEPENDS
      libc.include.llvm-libc-macros.stdio_ext_macros
      libc.src.__support.File.file
      libc.src.__support.printf_core.vfprintf_internal
      libc.src.stdio.scanf_core.vfscanf_internal
      libc.src.errno.errno
      ${thread_deps}
    COMPILE_OPTIONS -ULIBC_COPT_STDIO_USE_SYSTEM_FILE -DFile=LockingFile
    LINK_LIBRARIES ${thread_libs}
  )
endif()
