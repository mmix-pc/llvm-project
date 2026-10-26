if(LIBC_TARGET_OS STREQUAL "linux")
  add_object_library(
    paths
    SRCS paths.cpp
    HDRS paths.h
    DEPENDS .fields .fragments .results
      libc.src.__support.wchar.restartable_converter
      libc.hdr.errno_macros libc.hdr.glob_macros libc.src.__support.libc_errno
      libc.src.glob.glob libc.src.glob.globfree
  )
  if(LLVM_LIBC_FULL_BUILD)
    set(command_compile_options)
    if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND
       LIBC_MMIX_BUILD_CANCELLATION_RUNTIME_LOCKS)
      set(command_compile_options -fno-lto -fexceptions -funwind-tables)
    endif()
    add_object_library(
      command
      SRCS command.cpp
      HDRS command.h
      DEPENDS
        .source .syntax
        libc.src.stdio.linux.pipe_process
        libc.hdr.errno_macros
        libc.hdr.fcntl_macros
        libc.include.sys_syscall
        libc.src.__support.CPP.string_view
        libc.src.__support.error_or
        libc.src.__support.OSUtil.osutil
        libc.src.__support.OSUtil.linux.syscall_wrappers.close
        libc.src.__support.OSUtil.linux.syscall_wrappers.fcntl
        libc.src.errno.errno
        libc.src.spawn.posix_spawn
        libc.src.spawn.posix_spawn_file_actions_init
        libc.src.spawn.posix_spawn_file_actions_destroy
        libc.src.spawn.posix_spawn_file_actions_addclose
        libc.src.spawn.posix_spawn_file_actions_adddup2
        libc.src.spawn.posix_spawn_file_actions_addopen
        libc.src.unistd.read
      COMPILE_OPTIONS ${command_compile_options}
    )
  endif()
  add_object_library(
    environment
    SRCS environment.cpp
    HDRS environment.h
    DEPENDS .expand libc.src.pwd.getpwnam_r libc.src.stdlib.environ_internal
      libc.src.__support.str_to_integer libc.src.__support.integer_to_string
      libc.src.__support.CPP.limits libc.hdr.errno_macros
      libc.hdr.func.free libc.hdr.func.realloc
  )
endif()

add_object_library(
  source
  SRCS source.cpp
  HDRS source.h
  DEPENDS .syntax libc.src.__support.char_vector
    libc.src.__support.wchar.restartable_converter
    libc.src.__support.CPP.string_view libc.src.__support.CPP.limits
    libc.src.__support.alloc_checker libc.hdr.errno_macros
    libc.hdr.func.malloc libc.hdr.func.free
    libc.src.string.memory_utils.inline_memcpy
)

add_object_library(
  expand
  SRCS expand.cpp
  HDRS expand.h
  DEPENDS .arithmetic .dollar_quote .fragments .pattern .syntax
    libc.src.__support.integer_to_string libc.hdr.errno_macros
)

add_object_library(
  fragments
  SRCS fragments.cpp
  HDRS fragments.h
  DEPENDS .fields libc.src.__support.char_vector
    libc.src.__support.CPP.limits libc.src.string.memory_utils.inline_memcpy
    libc.hdr.errno_macros libc.hdr.func.free libc.hdr.func.realloc
)

add_object_library(
  pattern
  SRCS pattern.cpp
  HDRS pattern.h
  DEPENDS .parameter libc.src.fnmatch.matcher libc.src.__support.char_vector
    libc.src.__support.alloc_checker libc.src.__support.CPP.limits
    libc.hdr.errno_macros libc.hdr.func.free
)

add_object_library(
  dollar_quote
  SRCS dollar_quote.cpp
  HDRS dollar_quote.h
  DEPENDS libc.hdr.stdint_proxy libc.src.__support.CPP.span
)

add_entrypoint_object(
  wordfree
  SRCS wordfree.cpp
  HDRS wordfree.h
  DEPENDS .results libc.hdr.types.wordexp_t libc.src.__support.common
    libc.src.__support.libc_errno
)

add_object_library(
  results
  SRCS results.cpp
  HDRS results.h
  DEPENDS libc.hdr.types.wordexp_t libc.hdr.errno_macros
    libc.hdr.func.free libc.hdr.func.malloc libc.hdr.func.realloc
    libc.src.__support.CPP.limits libc.src.__support.CPP.string_view
    libc.src.string.memory_utils.inline_memcpy
)

add_object_library(
  shell_syntax
  SRCS shell_syntax.cpp
  HDRS shell_syntax.h
  DEPENDS .syntax .dollar_quote libc.src.__support.alloc_checker
    libc.hdr.errno_macros libc.hdr.func.free
)

add_object_library(
  syntax
  SRCS syntax.cpp
  HDRS syntax.h
  DEPENDS .dollar_quote libc.hdr.stdint_proxy libc.src.__support.CPP.span
)

add_object_library(
  parameter
  SRCS parameter.cpp
  HDRS parameter.h
  DEPENDS libc.hdr.errno_macros libc.src.__support.CPP.string_view
    libc.src.__support.error_or
)

add_object_library(
  ifs
  SRCS ifs.cpp
  HDRS ifs.h
  DEPENDS .fields libc.src.__support.wchar.restartable_converter
    libc.src.locale.character
    libc.src.__support.CPP.limits libc.src.__support.alloc_checker
    libc.src.string.memory_utils.inline_memcpy
    libc.hdr.errno_macros libc.hdr.func.free libc.hdr.func.malloc
)

add_object_library(
  fields
  SRCS fields.cpp
  HDRS fields.h
  DEPENDS libc.hdr.errno_macros libc.src.__support.CPP.limits
    libc.src.__support.CPP.span libc.src.__support.CPP.string_view
    libc.src.__support.error_or
)

add_object_library(
  arithmetic
  SRCS arithmetic.cpp
  HDRS arithmetic.h
  DEPENDS libc.hdr.errno_macros libc.src.__support.CPP.limits
    libc.src.__support.CPP.string_view libc.src.__support.error_or
)
