// Header-only adapter for LLVM's unconditional sys/types.h inclusion.
// Reuse real LLVM libc types; no POSIX or Linux UAPI surface is qualified.
#include <llvm-libc-types/size_t.h>
#include <llvm-libc-types/ssize_t.h>
