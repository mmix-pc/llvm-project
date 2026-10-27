add_object_library(
  decoded_text
  SRCS decoded_text.cpp
  HDRS decoded_text.h
  DEPENDS libc.hdr.types.regoff_t libc.hdr.errno_macros libc.hdr.func.free
    libc.src.__support.CPP.limits libc.src.__support.CPP.string_view
    libc.src.__support.alloc_checker
    libc.src.__support.wchar.restartable_converter
)

function(add_bsd_regex_engine)
  set(bsd_regex_dir "${CMAKE_CURRENT_SOURCE_DIR}/engine")
  set(bsd_regex_options
    -Dllvm_regcomp=__llvm_libc_bsd_regcomp
    -Dllvm_regexec=__llvm_libc_bsd_regexec
    -Dllvm_regfree=__llvm_libc_bsd_regfree
    -Dllvm_strlcpy=__llvm_libc_bsd_strlcpy
    -Dcalloc=__llvm_libc_regex_calloc)
  set(bsd_regex_dependencies)
  foreach(function malloc realloc free)
    list(APPEND bsd_regex_options -D${function}=__llvm_libc_regex_${function})
    list(APPEND bsd_regex_dependencies libc.hdr.func.${function})
  endforeach()
  foreach(function isalnum isalpha isdigit isupper islower tolower toupper)
    list(APPEND bsd_regex_options -D${function}=__llvm_libc_regex_${function})
    list(APPEND bsd_regex_dependencies libc.src.ctype.${function})
  endforeach()
  foreach(function strlen strncmp memset memmove memcmp)
    list(APPEND bsd_regex_options -D${function}=__llvm_libc_regex_${function})
    list(APPEND bsd_regex_dependencies libc.src.string.${function})
  endforeach()

  add_object_library(
    bsd_support
    SRCS bsd_support.cpp
    DEPENDS ${bsd_regex_dependencies}
  )
  add_object_library(
    bsd_engine
    SRCS
      ${bsd_regex_dir}/regcomp.c
      ${bsd_regex_dir}/regexec.c
      ${bsd_regex_dir}/regfree.c
      ${bsd_regex_dir}/regstrlcpy.c
    HDRS bsd_regex.h engine/regex_impl.h engine/regex2.h engine/regutils.h
      engine/regengine.inc engine/regminimal.inc
    # Preserve the imported C engine's warning profile for this derivative.
    COMPILE_OPTIONS ${bsd_regex_options} -Wno-conversion -Wno-unused-parameter
    DEPENDS .bsd_support libc.include.ctype libc.include.limits libc.include.stdint
      libc.include.stdio libc.include.stdlib libc.include.string libc.include.strings
      libc.include.assert
  )
endfunction()

foreach(entry regcomp regexec regfree)
  if("libc.src.regex.${entry}" IN_LIST TARGET_LIBC_ENTRYPOINTS)
    add_bsd_regex_engine()
    break()
  endif()
endforeach()

if(TARGET libc.src.regex.bsd_engine)
  add_object_library(
    locale_bracket
    SRCS locale_bracket.cpp
    HDRS locale_bracket.h
    DEPENDS libc.src.locale.character libc.src.locale.collation_key
      libc.hdr.func.free libc.src.__support.CPP.limits
      libc.src.__support.alloc_checker
  )
  add_object_library(
    character_regex
    SRCS character_regex.cpp
    HDRS character_regex.h
    DEPENDS .bsd_engine .decoded_text .locale_bracket libc.hdr.types.regmatch_t
      libc.src.locale.character
      libc.hdr.errno_macros libc.hdr.func.free
      libc.src.__support.CPP.limits libc.src.__support.alloc_checker
  )
  add_object_library(
    compiled_regex
    SRCS compiled_regex.cpp
    HDRS compiled_regex.h
    DEPENDS .character_regex libc.src.locale.locale_storage
  )
endif()

add_entrypoint_object(
  regcomp
  SRCS
    regcomp.cpp
  HDRS
    regcomp.h
  DEPENDS
    .compiled_regex
    libc.hdr.types.regex_t
    libc.hdr.regex_macros
    libc.src.__support.common
    libc.src.__support.CPP.new
    libc.src.__support.alloc_checker
)

add_entrypoint_object(
  regexec
  SRCS
    regexec.cpp
  HDRS
    regexec.h
  DEPENDS
    .compiled_regex
    libc.hdr.types.regex_t
    libc.hdr.types.regmatch_t
    libc.hdr.types.size_t
    libc.hdr.regex_macros
    libc.src.__support.common
)

add_entrypoint_object(
  regerror
  SRCS
    regerror.cpp
  HDRS
    regerror.h
  DEPENDS
    libc.hdr.types.regex_t
    libc.hdr.types.size_t
    libc.hdr.regex_macros
    libc.src.__support.common
    libc.src.__support.CPP.string_view
    libc.src.string.memory_utils.inline_memcpy
)

add_entrypoint_object(
  regfree
  SRCS
    regfree.cpp
  HDRS
    regfree.h
  DEPENDS
    .compiled_regex
    libc.hdr.types.regex_t
    libc.src.__support.common
    libc.src.__support.CPP.new
)
