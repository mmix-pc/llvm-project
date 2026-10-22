add_header(
  signal_macros
  HDR
    signal-macros.h
)

include(UnistdOptions.cmake)
set(unistd_options_header "${CMAKE_CURRENT_BINARY_DIR}/unistd-options.h")
mmix_configure_unistd_options("${unistd_options_header}")
add_header_library(unistd_options HDRS "${unistd_options_header}")
get_fq_target_name(unistd_options unistd_options_target)
set_target_properties(${unistd_options_target} PROPERTIES
  HEADER_FILE_PATH "${unistd_options_header}"
  HEADER_NAME "unistd-options.h")
add_header(unistd_macros HDR unistd-macros.h DEPENDS .unistd_options)

if(LIBC_TARGET_OS STREQUAL "linux")
  add_header(linux_limits_macros HDR linux-limits-macros.h)
  add_header(linux_features HDR linux-features.h)
  add_header(common_base HDR ../../__llvm-libc-common.h DEST_HDR common-base.h)
  if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    add_header(linux_signal_base_macros HDR linux-signal-macros.h
      DEST_HDR linux-signal-base-macros.h)
    add_header(linux_signal_macros HDR cancellation-signal-macros.h
      DEST_HDR linux-signal-macros.h DEPENDS .linux_signal_base_macros)
  else()
    add_header(linux_signal_macros HDR linux-signal-macros.h)
  endif()
  if(LIBC_MMIX_BUILD_UNWIND_ROOTS)
    add_header(pthread_base_macros HDR ../pthread-macros.h
      DEST_HDR pthread-base-macros.h)
    if(LIBC_MMIX_BUILD_ASYNC_CANCELLATION)
      add_header(pthread_cleanup_base HDR pthread-cleanup.h DEST_HDR pthread-cleanup-base.h)
      add_header(pthread_cleanup HDR pthread-cleanup-async.h DEST_HDR pthread-cleanup.h
        DEPENDS .pthread_cleanup_base)
    else()
      add_header(pthread_cleanup HDR pthread-cleanup.h)
    endif()
  endif()
endif()
