add_object_library(
  fenv_register
  SRCS fenv_register.cpp
  HDRS FEnvImpl.h
  DEPENDS
    libc.hdr.types.fenv_t
    libc.hdr.fenv_macros
    libc.hdr.math_macros
    libc.hdr.stdint_proxy
    libc.src.__support.CPP.bit
    libc.src.__support.CPP.type_traits
    libc.src.__support.macros.attributes
    libc.src.errno.errno
)
