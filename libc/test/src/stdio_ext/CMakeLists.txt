add_custom_target(libc_stdio_ext_unittests)

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
