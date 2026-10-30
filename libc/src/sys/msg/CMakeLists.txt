if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
  add_subdirectory(${LIBC_TARGET_OS})
endif()

add_entrypoint_object(
  msgget
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.msgget
)

add_entrypoint_object(
  msgsnd
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.msgsnd
)

add_entrypoint_object(
  msgrcv
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.msgrcv
)

add_entrypoint_object(
  msgctl
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.msgctl
)
