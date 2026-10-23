add_custom_target(libc_ftw_unittests)

add_libc_test(
  nftw_resource_test
  OVERLAY_BUILD_ONLY
  SUITE libc_ftw_unittests
  SRCS nftw_resource_test.cpp
  DEPENDS
    libc.hdr.types.FTS libc.hdr.types.struct_dirent libc.include.dirent
    libc.hdr.errno_macros libc.hdr.fcntl_macros libc.hdr.sys_stat_macros
    libc.hdr.func.malloc libc.hdr.func.free libc.hdr.func.realloc
    libc.hdr.limits_macros libc.hdr.stdint_proxy
    libc.src.__support.OSUtil.linux.stat.stat_via_statx
    libc.src.__support.OSUtil.linux.syscall_wrappers.chdir
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.fchdir
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.__support.OSUtil.osutil libc.src.__support.libc_assert
    libc.src.errno.errno libc.src.stdlib.qsort_util
    libc.src.string.memmove libc.src.string.memset
    libc.src.string.strlen libc.src.string.strrchr
  COMPILE_OPTIONS -fexceptions -funwind-tables -Wno-conversion
)
if(TARGET libc.test.src.ftw.nftw_resource_test.__build__)
  target_link_options(libc.test.src.ftw.nftw_resource_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
endif()

# Callback unwind uses the host C++ runtime in overlay tests.
add_libc_test(
  nftw_adapter_test
  OVERLAY_BUILD_ONLY
  SUITE libc_ftw_unittests
  SRCS nftw_adapter_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.signal_macros
    libc.hdr.sys_stat_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.chdir
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.fchdir
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.errno.errno
    libc.src.__support.OSUtil.osutil
  COMPILE_OPTIONS -fexceptions -funwind-tables
)

foreach(test nftw_test nftw_cancel_test)
add_libc_test(
  ${test}
  OVERLAY_BUILD_ONLY
  SUITE libc_ftw_unittests
  SRCS ${test}.cpp
    ${LIBC_SOURCE_DIR}/src/ftw/nftw.cpp
    ${LIBC_SOURCE_DIR}/src/ftw/ftw.cpp
    ${LIBC_SOURCE_DIR}/src/ftw/linux/walk.cpp
  DEPENDS
    libc.src.fts.engine
    libc.src.__support.OSUtil.linux.syscall_wrappers.chdir
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.fchdir
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.errno.errno
    libc.src.fcntl.fcntl libc.src.fcntl.open
    libc.src.stdio.rename libc.src.stdio.snprintf
    libc.src.__support.OSUtil.osutil
    libc.src.stdlib.mkdtemp
    libc.src.string.strcmp libc.src.string.strcpy libc.src.string.strlen
    libc.src.sys.stat.mkdir libc.src.sys.stat.mkfifo libc.src.sys.stat.stat
    libc.src.unistd.chdir libc.src.unistd.close libc.src.unistd.getcwd
    libc.src.unistd.rmdir libc.src.unistd.symlink libc.src.unistd.unlink
  COMPILE_OPTIONS -fexceptions -funwind-tables
  LINK_LIBRARIES pthread
)
endforeach()
