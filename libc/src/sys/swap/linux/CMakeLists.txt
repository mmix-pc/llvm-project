
add_entrypoint_object(swapon SRCS swapon.cpp HDRS ../swapon.h DEPENDS libc.include.sys_syscall libc.src.__support.OSUtil.osutil libc.src.errno.errno)

add_entrypoint_object(swapoff SRCS swapoff.cpp HDRS ../swapoff.h DEPENDS libc.include.sys_syscall libc.src.__support.OSUtil.osutil libc.src.errno.errno)
