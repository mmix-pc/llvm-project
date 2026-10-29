if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
endif()

# Keep the shared action layout header-only for child executors. Link sysconf
# from addopen and adddup2, which use the shared fd-range helper.
add_header_library(
  file_actions
  HDRS
    file_actions.h
  DEPENDS
    libc.hdr.stdint_proxy
    libc.hdr.unistd_macros
    libc.include.spawn
    libc.src.__support.libc_errno
)

add_entrypoint_object(
  posix_spawn_file_actions_init
  SRCS
    posix_spawn_file_actions_init.cpp
  HDRS
    posix_spawn_file_actions_init.h
  DEPENDS
    libc.include.spawn
)

add_entrypoint_object(
  posix_spawn_file_actions_destroy
  SRCS
    posix_spawn_file_actions_destroy.cpp
  HDRS
    posix_spawn_file_actions_destroy.h
  DEPENDS
    .file_actions
    libc.include.spawn
    libc.src.__support.CPP.new
    libc.src.errno.errno
)

add_entrypoint_object(
  posix_spawn_file_actions_adddup2
  SRCS
    posix_spawn_file_actions_adddup2.cpp
  HDRS
    posix_spawn_file_actions_adddup2.h
  DEPENDS
    .file_actions
    libc.include.spawn
    libc.src.__support.CPP.new
    libc.src.errno.errno
    libc.src.unistd.sysconf
)

add_entrypoint_object(
  posix_spawn_file_actions_addopen
  SRCS
    posix_spawn_file_actions_addopen.cpp
  HDRS
    posix_spawn_file_actions_addopen.h
  DEPENDS
    .file_actions
    libc.include.spawn
    libc.src.__support.CPP.new
    libc.src.errno.errno
    libc.src.string.memory_utils.inline_memcpy
    libc.src.string.string_utils
    libc.src.unistd.sysconf
)

add_entrypoint_object(
  posix_spawn_file_actions_addclose
  SRCS
    posix_spawn_file_actions_addclose.cpp
  HDRS
    posix_spawn_file_actions_addclose.h
  DEPENDS
    .file_actions
    libc.include.spawn
    libc.src.__support.CPP.new
    libc.src.errno.errno
)

add_entrypoint_object(
  posix_spawn_file_actions_addchdir
  SRCS
    posix_spawn_file_actions_addchdir.cpp
  HDRS
    posix_spawn_file_actions_addchdir.h
  DEPENDS
    .file_actions
    libc.include.spawn
    libc.src.__support.CPP.new
    libc.src.errno.errno
    libc.src.string.memory_utils.inline_memcpy
    libc.src.string.string_utils
)

add_entrypoint_object(
  posix_spawn_file_actions_addfchdir
  SRCS
    posix_spawn_file_actions_addfchdir.cpp
  HDRS
    posix_spawn_file_actions_addfchdir.h
  DEPENDS
    .file_actions
    libc.include.spawn
    libc.src.__support.CPP.new
    libc.src.errno.errno
)

add_entrypoint_object(
  posix_spawn
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.posix_spawn
)

add_entrypoint_object(
  posix_spawnp
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.posix_spawnp
)
