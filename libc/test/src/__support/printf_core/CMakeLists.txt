add_libc_test(
  parser_test
  SUITE
    libc-support-tests
  SRCS
    parser_test.cpp
  DEPENDS
    libc.src.__support.printf_core.parser
    libc.src.__support.printf_core.core_structs
    libc.src.__support.CPP.string_view
    libc.src.__support.arg_list
    libc.test.UnitTest.PrintfMatcher
)

add_libc_test(
  writer_test
  SUITE
    libc-support-tests
  SRCS
    writer_test.cpp
  DEPENDS
    libc.src.__support.printf_core.writer
    libc.src.string.memory_utils.inline_memcpy
    libc.src.__support.CPP.string_view
)

foreach(mode IN ITEMS default no_index small_index)
  set(parser_options "")
  if(mode STREQUAL "no_index")
    list(APPEND parser_options -DLIBC_COPT_PRINTF_DISABLE_INDEX_MODE)
  elseif(mode STREQUAL "small_index")
    list(APPEND parser_options -DLIBC_COPT_PRINTF_INDEX_ARR_LEN=1)
  endif()
  add_libc_test(
    wide_parser_${mode}_test
    SUITE
      libc-support-tests
    SRCS
      wide_parser_test.cpp
    COMPILE_OPTIONS
      ${parser_options}
    DEPENDS
      libc.hdr.types.wint_t
      libc.src.__support.printf_core.parser
      libc.src.__support.printf_core.core_structs
      libc.src.__support.CPP.bit
      libc.src.__support.arg_list
  )
endforeach()

add_libc_test(
  converter_test
  SUITE
    libc-support-tests
  SRCS
    converter_test.cpp
  DEPENDS
    libc.src.__support.printf_core.converter
    libc.src.__support.printf_core.writer
    libc.src.__support.printf_core.core_structs
)
