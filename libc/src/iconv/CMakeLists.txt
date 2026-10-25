if(NOT LIBC_TARGET_OS_IS_LINUX)
  return()
endif()

add_object_library(
  context
  SRCS context.cpp
  HDRS context.h
  DEPENDS libc.src.__support.wchar.encoding_descriptor
    libc.src.locale.builtin_transliteration libc.src.locale.locale
    libc.src.locale.linux_locale_file libc.src.__support.alloc_checker
    libc.hdr.func.free libc.hdr.errno_macros
)

foreach(name iconv_open iconv_close iconv)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS .context libc.hdr.types.iconv_t libc.hdr.types.size_t
      libc.src.__support.libc_errno
  )
endforeach()
