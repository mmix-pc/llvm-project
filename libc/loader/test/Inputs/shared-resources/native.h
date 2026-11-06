// Native injection declarations: resource syscalls are supplied by the test.
// The unrelated target startup function is discarded by section GC. Overlay
// headers do not expose libc's private atexit typedef, whose signature is kept.
#ifndef LLVM_LIBC_LOADER_TEST_SHARED_RESOURCES_NATIVE_H
#define LLVM_LIBC_LOADER_TEST_SHARED_RESOURCES_NATIVE_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_MMIX_SYSCALL_H
extern "C" long __llvm_libc_mmix_syscall(unsigned long, long, long, long, long,
                                         long, long);
#define LLVM_LIBC_HDR_TYPES_ATEXITHANDLER_T_H
using __atexithandler_t = void (*)();
#endif
