# LLVM Libc Loader

This experimental component is intended to provide process-interpreter and
dynamic-loading support for LLVM libc. Shared algorithms are separated from
operating-system and architecture policy so that additional targets can reuse
the loader without duplicating its implementation.

## Organization

Common Linux ELF parsing, mapping, dependency, symbol-resolution and lifecycle
algorithms belong in `linux/`. Architecture-specific policy, relocations and
entry/transfer assembly belong in `linux/<architecture>/`. Target interfaces,
such as `linux/target.h`, separate shared algorithms from those implementations.
These responsibilities describe the intended organization, not completed
functionality.

`CompileOptions.cmake` scopes freestanding options to loader objects. Bootstrap
must not depend on initialized libc, TLS or public allocation. Final dependency
and relocation audits remain necessary: compiler flags alone cannot establish
a bootstrap-safe interpreter.

## Build And Tests

`LIBC_BUILD_LOADER=ON` enables loader components in a normal runtimes/libc build.
The default is OFF. Build `libc-loader` explicitly; components are not part of
`libc.a` or the default installation. Enabled builds require a supported target.

Host tests use a separate native build, with an explicit host Clang:

```sh
cmake -S libc/loader/test -B build/loader-host-tests -G Ninja \
  -DCMAKE_CXX_COMPILER="$HOST_CLANGXX"
cmake --build build/loader-host-tests --parallel 4
ctest --test-dir build/loader-host-tests --output-on-failure
```

These tests run target policy on decoded host values, check configuration
admission and inject syscall results into production bootstrap support code.
They do not qualify target execution or byte-order-aware file decoding.

## Bootstrap Support

Linux bootstrap helpers preserve raw negative syscall errors without errno or
TLS. I/O reports partial progress and retries interrupted reads/writes.
Descriptor ownership is consumed on close, including errors, so cleanup never
retries a potentially reused descriptor. Fatal startup writes a fixed diagnostic
with at most eight attempts, calls exit_group(127), and traps if it returns.
The attempt limit does not bound time spent in a blocking syscall.

The private mmap-backed arena accepts an explicit validated page size, checks
allocation arithmetic and supports suballocation without public malloc/free.
Owners must call reset explicitly; failed unmaps remain owned for retry. All
previous allocations become invalid on reset, including partial failure.
There is no individual free or synchronization: this is startup-only storage,
not yet a concurrent dynamic-loading allocator.

## Current Target Support

Only MMIX Linux is currently admitted, using Clang. Its implementation in
`linux/mmix/` contains machine identity admission on decoded ELF headers and a
raw syscall adapter reusing libc's hidden MMIX assembly leaf. It does not yet
parse input, relocate images or start a process. Support for other
architectures requires their target implementations and validation.

The intended MMIX interpreter artifact is `ld-llvm-mmix.so.1`. No executable or
install rule is provided until entry and self-relocation exist; the current
build does not provide a runnable dynamic loader.
