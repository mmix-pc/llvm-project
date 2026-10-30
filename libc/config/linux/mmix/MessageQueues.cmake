option(LIBC_MMIX_BUILD_MESSAGE_QUEUES "Build Linux POSIX message queue providers" OFF)
if(NOT LIBC_MMIX_BUILD_MESSAGE_QUEUES)
  return()
endif()

list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.mqueue.mq_open
  libc.src.mqueue.mq_close
  libc.src.mqueue.mq_unlink
  libc.src.mqueue.mq_getattr
  libc.src.mqueue.mq_setattr
  libc.src.mqueue.mq_send
  libc.src.mqueue.mq_receive
  libc.src.mqueue.mq_timedsend
  libc.src.mqueue.mq_timedreceive)
