add_subdirectory(testdata)
add_custom_target(libc_dirent_unittests)

if(LIBC_TARGET_OS_IS_LINUX AND NOT LLVM_LIBC_FULL_BUILD)
  add_libc_test(
    scandir_ownership_test
    OVERLAY_BUILD_ONLY
    SUITE libc_dirent_unittests
    SRCS scandir_ownership_test.cpp
      ${LIBC_SOURCE_DIR}/src/__support/File/dir.cpp
      ${LIBC_SOURCE_DIR}/src/dirent/readdir_r.cpp
    DEPENDS
      libc.src.__support.CPP.new
      libc.src.__support.CPP.mutex
      libc.src.__support.CPP.span
      libc.src.__support.CPP.limits
      libc.src.__support.threads.mutex
      libc.src.__support.alloc_checker
      libc.src.__support.error_or
      libc.src.stdlib.qsort_util
      libc.src.string.memory_utils.inline_memcpy
      libc.src.errno.errno
      libc.hdr.types.struct_dirent
    COMPILE_OPTIONS -fexceptions -funwind-tables
    LINK_LIBRARIES pthread
      "-Wl,--wrap=malloc,--wrap=realloc,--wrap=free"
  )
endif()

add_libc_test(
  directory_operations_test
  FULL_BUILD_ONLY
  SUITE libc_dirent_unittests
  SRCS directory_operations_test.cpp
  DEPENDS
    libc.src.dirent.alphasort
    libc.src.dirent.closedir
    libc.src.dirent.dirfd
    libc.src.dirent.fdopendir
    libc.src.dirent.opendir
    libc.src.dirent.readdir
    libc.src.dirent.readdir_r
    libc.src.dirent.rewinddir
    libc.src.dirent.scandir
    libc.src.dirent.scandirat
    libc.src.dirent.seekdir
    libc.src.dirent.telldir
    libc.src.dirent.versionsort
    libc.src.fcntl.open
    libc.src.string.strcpy
    libc.src.unistd.close
    libc.src.unistd.lseek
    libc.src.errno.errno
)

add_libc_test(
  dirent_test
  FULL_BUILD_ONLY
  SUITE
    libc_dirent_unittests
  SRCS
    dirent_test.cpp
  DEPENDS
    libc.src.__support.CPP.string_view
    libc.src.dirent.closedir
    libc.src.dirent.dirfd
    libc.src.dirent.opendir
    libc.src.dirent.readdir
    libc.src.errno.errno
    libc.test.UnitTest.ErrnoCheckingTest
)

add_libc_test(
  fdopendir_test
  FULL_BUILD_ONLY
  SUITE
    libc_dirent_unittests
  SRCS
    fdopendir_test.cpp
  DEPENDS
    libc.src.__support.CPP.string_view
    libc.src.dirent.fdopendir
    libc.src.dirent.closedir
    libc.src.dirent.readdir
    libc.src.fcntl.open
    libc.src.unistd.close
    libc.src.fcntl.fcntl
    libc.src.errno.errno
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
    libc.hdr.types.DIR
    libc.hdr.types.struct_dirent
)
