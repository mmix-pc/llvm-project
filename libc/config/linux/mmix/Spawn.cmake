option(LIBC_MMIX_BUILD_SPAWN "Build the Linux single-thread null-attribute spawn provider" OFF)
if(NOT LIBC_MMIX_BUILD_SPAWN)
  return()
endif()
if(NOT LIBC_MMIX_BUILD_FORK OR NOT LIBC_MMIX_BUILD_DESCRIPTORS OR
   NOT LIBC_MMIX_BUILD_ALLOCATION)
  message(FATAL_ERROR "MMIX Linux spawn requires fork, descriptor and allocation providers")
endif()
list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.spawn.posix_spawn
  libc.src.spawn.posix_spawn_file_actions_init
  libc.src.spawn.posix_spawn_file_actions_destroy
  libc.src.spawn.posix_spawn_file_actions_addopen
  libc.src.spawn.posix_spawn_file_actions_adddup2
  libc.src.spawn.posix_spawn_file_actions_addclose
)
