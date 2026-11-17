if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
  add_subdirectory(${LIBC_TARGET_OS})
endif()

if(LIBC_TARGET_OS_IS_LINUX)
  add_libc_test(
    ifaddrs_test
    SUITE libc_net_unittests
    SRCS ifaddrs_test.cpp
      ${LIBC_SOURCE_DIR}/src/net/linux/getifaddrs.cpp
      ${LIBC_SOURCE_DIR}/src/net/linux/freeifaddrs.cpp
    DEPENDS libc.src.net.linux.ifaddrs_impl
      libc.src.__support.OSUtil.linux.network_syscall_policy
      libc.hdr.func.malloc libc.hdr.func.free libc.src.errno.errno
  )
endif()
