# MMIX has no protection-key instructions; keep the generic error provider.
add_header_library(
  pkey_common
  HDRS ../generic/pkey_common.h
  DEPENDS libc.src.sys.mman.linux.generic.pkey_common
)
