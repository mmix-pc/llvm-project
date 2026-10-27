add_object_library(
  catalog_data
  SRCS catalog_data.cpp
  HDRS catalog_data.h
  DEPENDS libc.hdr.stdint_proxy libc.hdr.errno_macros libc.hdr.func.free
    libc.src.__support.CPP.span libc.src.__support.error_or
    libc.src.__support.alloc_checker libc.src.stdlib.qsort_util
)

add_object_library(
  catalog_path
  SRCS catalog_path.cpp
  HDRS catalog_path.h
  DEPENDS .catalog_data libc.src.__support.CPP.string_view
)

# Shared message helpers also serve overlay consumers. Public catalog handles
# belong to the full build and must not replace the host libc's representation.
if(NOT LLVM_LIBC_FULL_BUILD)
  return()
endif()

set(catopen_source catopen.cpp)
set(catopen_deps)
if(LIBC_TARGET_OS_IS_LINUX)
  set(catopen_source linux/catopen.cpp)
  list(APPEND catopen_deps .catalog_path libc.src.locale.locale
    libc.src.stdlib.environ_internal libc.src.__support.OSUtil.linux.auxv
    libc.hdr.sys_stat_macros
    libc.src.__support.OSUtil.linux.stat.kernel_statx_types
    libc.src.__support.OSUtil.linux.syscall_wrappers.statx)
endif()
add_entrypoint_object(
  catopen
  SRCS
    ${catopen_source}
  HDRS
    catopen.h
  DEPENDS
    ${catopen_deps}
    libc.include.llvm-libc-macros.nl_types_macros
    libc.include.llvm-libc-types.nl_catd
    libc.src.errno.errno
)

add_entrypoint_object(
  catclose
  SRCS
    catclose.cpp
  HDRS
    catclose.h
  DEPENDS
    .catalog_data libc.src.__support.libc_errno
    libc.include.llvm-libc-types.nl_catd
)

add_entrypoint_object(
  catgets
  SRCS
    catgets.cpp
  HDRS
    catgets.h
  DEPENDS
    .catalog_data libc.src.__support.libc_errno
    libc.include.llvm-libc-types.nl_catd
)
