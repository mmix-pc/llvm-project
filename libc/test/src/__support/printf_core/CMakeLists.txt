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

add_libc_test(
  wide_writer_test
  SUITE
    libc-support-tests
  SRCS
    wide_writer_test.cpp
  DEPENDS
    libc.src.__support.printf_core.wide_writer
    libc.src.__support.CPP.limits
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
  wide_converter_test
  SUITE
    libc-support-tests
  SRCS
    wide_converter_test.cpp
  DEPENDS
    libc.src.__support.printf_core.wide_converter
    libc.src.stdio.snprintf
    libc.src.__support.printf_core.parser
    libc.src.__support.arg_list
    libc.test.UnitTest.FPTestHelpers
)

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

# Exercise module instantiation independently of the platform's stdio policy.
foreach(mode IN ITEMS modular modular_no_float modular_float320)
  set(converter_options -DLIBC_COPT_PRINTF_MODULAR)
  if(mode STREQUAL "modular_no_float")
    list(APPEND converter_options -DLIBC_COPT_PRINTF_DISABLE_FLOAT)
  elseif(mode STREQUAL "modular_float320")
    list(APPEND converter_options -DLIBC_COPT_FLOAT_TO_STR_USE_FLOAT320)
  endif()
  add_libc_test(
    wide_converter_${mode}_test
    SUITE
      libc-support-tests
    SRCS
      wide_converter_test.cpp
      ${LIBC_SOURCE_DIR}/src/__support/printf_core/float_impl.cpp
    COMPILE_OPTIONS
      ${converter_options}
    DEPENDS
      libc.src.__support.printf_core.wide_converter
      libc.src.stdio.snprintf
      libc.src.__support.printf_core.parser
      libc.src.__support.arg_list
      libc.test.UnitTest.FPTestHelpers
  )
endforeach()
