# The Linux CRT is preparation until the complete libc profile is published.
# FIXME: Revisit the unprotected TLS-free bootstrap and guard ownership for
# pthread/TLS and dynamic-loader integration; normal runtime code stays protected.
add_startup_object(
  start
  SRC start.cpp
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector
)

add_startup_object(
  process_args
  SRC process_args.cpp
  DEPENDS
    libc.config.app_h
    libc.hdr.elf_proxy
    libc.src.__support.OSUtil.linux.auxv
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector
)

# Environment/name storage is supplied by the existing libc providers when
# the complete CRT is composed; preparation does not export new entrypoints.
add_startup_object(
  bootstrap
  SRC bootstrap.cpp
  DEPENDS
    .process_args
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector
)

add_startup_object(
  stack_guard
  SRC stack_guard.cpp
  DEPENDS
    libc.src.__support.OSUtil.linux.mmix.syscall
    libc.include.sys_syscall
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector
)

# Guard initialization precedes this ordinary protected runtime path.
add_startup_object(
  lifecycle
  SRC lifecycle.cpp
  DEPENDS
    libc.config.app_h
    libc.hdr.types.size_t
    libc.src.__support.threads.linux.mmix.main_thread
  COMPILE_OPTIONS -ffreestanding -fno-builtin
)

# Keep libc state, termination and syscall providers in the runtime archive.
merge_relocatable_object(
  crt1
  .start
  .process_args
  .bootstrap
  .stack_guard
  .lifecycle
)

add_custom_target(libc-startup)
add_dependencies(libc-startup libc.startup.linux.mmix.crt1)
