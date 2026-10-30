add_header_library(
  types
  HDRS
    types.h
  DEPENDS
    libc.include.llvm-libc-types.mqd_t
    libc.include.llvm-libc-types.struct_mq_attr
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
  add_subdirectory(${LIBC_TARGET_OS})
endif()

add_entrypoint_object(
  mq_open
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.mq_open
)

add_entrypoint_object(
  mq_notify
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.mq_notify
)

add_entrypoint_object(
  mq_close
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.mq_close
)

add_entrypoint_object(
  mq_unlink
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.mq_unlink
)

add_entrypoint_object(
  mq_getattr
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.mq_getattr
)

add_entrypoint_object(
  mq_setattr
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.mq_setattr
)

add_entrypoint_object(
  mq_send
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.mq_send
)

add_entrypoint_object(
  mq_receive
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.mq_receive
)

add_entrypoint_object(
  mq_timedsend
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.mq_timedsend
)

add_entrypoint_object(
  mq_timedreceive
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.mq_timedreceive
)
