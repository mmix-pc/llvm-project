add_custom_target(libc-fmtmsg-tests)

if(NOT LLVM_LIBC_FULL_BUILD AND LIBC_TARGET_OS_IS_LINUX AND NOT CMAKE_CROSSCOMPILING)
  add_libc_test(
    fmtmsg_test
    OVERLAY_BUILD_ONLY
    SUITE libc-fmtmsg-tests
    SRCS fmtmsg_test.cpp
    DEPENDS
      libc.hdr.errno_macros
      libc.hdr.fcntl_macros
      libc.hdr.stdio_macros
      libc.src.__support.CPP.string_view
      libc.src.__support.File.file
      libc.src.__support.OSUtil.linux.syscall_wrappers.close
      libc.src.__support.OSUtil.linux.syscall_wrappers.open
      libc.src.__support.OSUtil.linux.syscall_wrappers.write
      libc.src.__support.common
      libc.src.__support.macros.config
    COMPILE_OPTIONS -ULIBC_COPT_STDIO_USE_SYSTEM_FILE
    LINK_LIBRARIES pthread
  )
endif()
