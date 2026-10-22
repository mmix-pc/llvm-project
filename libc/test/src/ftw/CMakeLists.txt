add_custom_target(libc_ftw_unittests)

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
