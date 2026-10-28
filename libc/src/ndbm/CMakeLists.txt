add_object_library(
  hash_validation
  SRCS hash_validation.cpp
  HDRS hash_validation.h
  DEPENDS libc.hdr.types.size_t libc.hdr.stdint_proxy
    libc.src.__support.macros.config
)

add_object_library(
  hash_page
  SRCS hash_page.cpp
  HDRS hash_page.h
  DEPENDS .hash_validation
    libc.src.string.memory_utils.inline_memcpy
    libc.src.string.memory_utils.inline_memmove
)
