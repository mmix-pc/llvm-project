if(LIBC_TARGET_OS_IS_LINUX)
  add_object_library(
    message_file
    SRCS linux/message_file.cpp
    HDRS message_file.h
    DEPENDS .mo_catalog libc.hdr.fcntl_macros libc.hdr.sys_stat_macros
      libc.src.__support.alloc_checker
      libc.src.__support.OSUtil.linux.stat.kernel_statx_types
      libc.src.__support.OSUtil.linux.syscall_wrappers.statx
      libc.src.__support.OSUtil.linux.syscall_wrappers.read
      libc.src.__support.OSUtil.linux.syscall_wrappers.close
  )
  add_object_library(
    message_path
    SRCS message_path.cpp
    HDRS message_path.h
    DEPENDS .mo_catalog libc.src.nl_types.catalog_path
  )
endif()

if(LIBC_TARGET_OS_IS_LINUX AND LLVM_LIBC_FULL_BUILD)
  add_object_library(
    message_lookup
    SRCS linux/message_lookup.cpp
    HDRS message_lookup.h
    DEPENDS .domain_bindings .message_file .message_path .message_results
      .message_translation libc.src.locale.locale
      libc.src.locale.linux_locale_file libc.src.locale.builtin_transliteration
      libc.src.stdlib.environ_internal libc.src.__support.libc_errno
      libc.src.__support.OSUtil.linux.secure_execution
  )
  foreach(name gettext dgettext dcgettext ngettext dngettext dcngettext
               gettext_l dgettext_l dcgettext_l ngettext_l dngettext_l dcngettext_l)
    add_entrypoint_object(
      ${name}
      SRCS ${name}.cpp
      HDRS ${name}.h
      DEPENDS .message_lookup libc.hdr.locale_macros
    )
  endforeach()
endif()

add_object_library(
  message_results
  SRCS message_results.cpp
  HDRS message_results.h
  DEPENDS libc.hdr.types.size_t
    libc.src.__support.error_or libc.src.__support.threads.mutex
    libc.src.__support.CPP.mutex libc.src.__support.CPP.string_view
    libc.src.__support.alloc_checker libc.hdr.errno_macros libc.hdr.func.free
)

add_object_library(
  message_translation
  SRCS message_translation.cpp
  HDRS message_translation.h
  DEPENDS .mo_catalog .plural_forms
    libc.hdr.func.free libc.hdr.func.realloc libc.hdr.errno_macros
    libc.src.__support.CPP.optional libc.src.__support.CPP.limits
    libc.src.__support.wchar.encoding_descriptor
    libc.src.locale.transliteration_data
)

add_object_library(
  plural_forms
  SRCS plural_forms.cpp
  HDRS plural_forms.h
  DEPENDS libc.hdr.errno_macros libc.hdr.stdint_proxy
    libc.src.__support.CPP.limits libc.src.__support.CPP.string_view
    libc.src.__support.error_or
)

add_object_library(
  domain_bindings
  SRCS domain_bindings.cpp
  HDRS domain_bindings.h
  DEPENDS libc.hdr.errno_macros libc.hdr.func.free
    libc.src.__support.alloc_checker libc.src.__support.error_or
    libc.src.__support.CPP.limits libc.src.__support.CPP.mutex
    libc.src.__support.CPP.string_view libc.src.__support.threads.mutex
)

foreach(name textdomain bindtextdomain bind_textdomain_codeset)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS .domain_bindings libc.src.__support.libc_errno
  )
endforeach()

add_object_library(
  mo_catalog
  SRCS mo_catalog.cpp
  HDRS mo_catalog.h
  DEPENDS
    libc.hdr.stdint_proxy
    libc.hdr.errno_macros
    libc.hdr.func.free
    libc.src.__support.alloc_checker
    libc.src.__support.CPP.atomic
    libc.src.__support.CPP.span
    libc.src.__support.CPP.string_view
    libc.src.__support.error_or
    libc.src.__support.macros.config
)
