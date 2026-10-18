add_object_library(
  langinfo
  SRCS langinfo.cpp
  HDRS langinfo.h
  DEPENDS libc.hdr.langinfo_macros libc.hdr.types.nl_item libc.src.locale.locale
)
add_entrypoint_object(
  nl_langinfo
  SRCS nl_langinfo.cpp
  HDRS nl_langinfo.h
  DEPENDS .langinfo libc.src.locale.locale
)
add_entrypoint_object(
  nl_langinfo_l
  SRCS nl_langinfo_l.cpp
  HDRS nl_langinfo_l.h
  DEPENDS .langinfo
)
