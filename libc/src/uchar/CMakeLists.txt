add_header_library(
  conversion
  HDRS conversion.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.types.size_t
    libc.src.__support.common
    libc.src.__support.libc_errno
    libc.src.__support.wchar.character_converter
)
foreach(name IN ITEMS c16rtomb c32rtomb mbrtoc16 mbrtoc32)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS
      .conversion
      libc.hdr.types.char16_t
      libc.hdr.types.char32_t
      libc.hdr.types.mbstate_t
      libc.hdr.types.size_t
      libc.src.__support.macros.config
  )
endforeach()
