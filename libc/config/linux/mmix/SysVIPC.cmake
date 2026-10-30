option(LIBC_MMIX_BUILD_SYSV_IPC "Build Linux SysV IPC providers" OFF)
if(NOT LIBC_MMIX_BUILD_SYSV_IPC)
  return()
endif()

list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.sys.msg.msgget
  libc.src.sys.msg.msgctl
  libc.src.sys.msg.msgsnd
  libc.src.sys.msg.msgrcv
  libc.src.sys.shm.shmget
  libc.src.sys.shm.shmctl
  libc.src.sys.shm.shmat
  libc.src.sys.shm.shmdt
  libc.src.sys.sem.semget
  libc.src.sys.sem.semctl
  libc.src.sys.sem.semop)
