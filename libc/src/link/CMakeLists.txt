add_entrypoint_object(
  _r_debug
  SRCS
    _r_debug.cpp
  HDRS
    _r_debug.h
  DEPENDS
    libc.hdr.types.struct_r_debug
    libc.src.__support.common
    libc.src.__support.macros.config
)

if(LIBC_TARGET_OS STREQUAL "linux" AND
   LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND LIBC_MMIX_BUILD_SHARED_LIBC)
  list(APPEND TARGET_ENTRYPOINT_NAME_LIST dl_iterate_phdr)
  add_entrypoint_object(dl_iterate_phdr ALIAS
    DEPENDS libc.src.dlfcn.linux.mmix.dl_iterate_phdr)
else()
  add_entrypoint_object(
    dl_iterate_phdr
    SRCS
      dl_iterate_phdr.cpp
    HDRS
      dl_iterate_phdr.h
    DEPENDS
      libc.hdr.stdint_proxy
      libc.include.elf
      libc.include.llvm-libc-macros.link_macros
      libc.src.__support.CPP.span
      libc.src.__support.OSUtil.linux.auxv
  )
endif()
