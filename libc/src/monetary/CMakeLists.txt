add_object_library(
  formatter
  SRCS formatter.cpp
  HDRS formatter.h
  DEPENDS libc.src.locale.monetary_data libc.src.__support.arg_list
    libc.src.__support.error_or libc.src.__support.alloc_checker
    libc.src.__support.printf_core.converter libc.hdr.func.free
    libc.src.string.memory_utils.inline_memcpy
    libc.src.string.memory_utils.inline_memset
)

if(NOT LLVM_LIBC_FULL_BUILD)
  return()
endif()

foreach(name strfmon strfmon_l)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS .formatter libc.src.locale.locale
      libc.src.__support.libc_errno libc.hdr.types.size_t
      libc.hdr.types.ssize_t libc.include.llvm-libc-types.locale_t
  )
endforeach()
