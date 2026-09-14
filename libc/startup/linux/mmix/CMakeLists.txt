# The Linux entry is preparation only until the complete CRT is composed.
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
