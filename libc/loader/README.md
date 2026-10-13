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
admission, parse ELF byte fixtures and inject syscall results into production
bootstrap support code. They do not qualify target execution.

## ELF Views

`linux/elf.h` provides checked ELF64 views without native-struct casts or
section-header dependencies. File views validate declared file extents; mapped
views accept independently established readable regions and recheck metadata
without claiming to reconstruct kernel mapping provenance. Input storage must
remain readable and alive; ELF metadata remains immutable while relocation may
modify admitted writable data. Failed opens invalidate the output view.

Admission distinguishes dynamic ET_EXEC, PIE, interpreter and DSO roles. Checks
cover load permissions, alignment, disjoint rounded pages, metadata provenance,
the interpreter path, bounded dynamic termination, stack policy, RELRO and
executable entry. Link-time addresses, file offsets and checked bias-adjusted
runtime addresses remain separate. Target hooks own machine identity, page
size, user-address bounds, entry alignment and interpreter pathname.

The byte decoders support either ELF64 byte order; target admission still
selects the supported machine. Dependency relocation, loading, symbol
resolution and startup transfer are separate implementation steps.

## Dynamic Metadata

`linux/dynamic.h` admits immutable dynamic metadata without allocations or
section headers. It preserves ordered NEEDED entries, validates singleton tags
and flags, bounded strings and ordinary symbols, RELA geometry and writable
destinations, and file-backed lifecycle-array views. Failed opens invalidate
the result. INIT/FINI addresses must be executable; PREINIT is main-only.
Array pointer contents are interpreted only after relocation, not by this view.

SysV symbol counts and GNU chain-derived counts are bounded by actual readable
file-backed storage. Hash checks cover indexes, termination/cycles, bucket and
name agreement, GNU bloom bits, reachable definitions and agreement when both
formats are present. Contiguous readable loads can cover a table, while holes,
BSS and bytes beyond a supplied mapped region cannot extend it. The parser
rejects deferred TLS, version, IFUNC, GNU_UNIQUE, PLT and unknown semantics.
It does not resolve symbols across images or call lifecycle functions.

`dynamic_test.cpp` exercises sectionless synthetic inputs and malformed
metadata. `loader-linked-image-test` also parses actual linked fixtures with
section metadata removed and rejects mutations of their hash, string, symbol
and relocation tables; its optional `shared` argument selects the DSO fixture.

## Dependency Mappings

`linux/mapping.h` owns one DSO transaction for an exact pathname. It stages the
seekable file in private anonymous storage and validates ELF and dynamic
metadata before reserving any image address range. Input files must remain
stable throughout loading; this is not protection against concurrent file
truncation or mutation. The temporary full-file copy is released before a
successful return and is not retained as a second loaded image.

The mapper reserves the full load span with sufficient over-alignment padding,
checks the actual load bias, and replaces only pages inside that reservation.
File-backed pages use private mappings; partial-page BSS is explicitly zeroed
and remaining BSS uses anonymous mappings. Holes and alignment padding remain
PROT_NONE. Temporary zero-fill access is RW, never W+X, before restoring final
segment permissions. RELRO protection follows relocation in later startup work.
Mapped metadata is revalidated before exposing the image and dynamic views.

Failure invalidates both views, consumes the fd once, and releases owned
storage. A single reservation owns all replacement mappings and holes; no
segment is separately unmapped. Failed unmaps remain owned for explicit reset,
and the object cannot be reused while cleanup remains pending. As with Arena,
callers must reset before destruction. There is no API to adopt or release
kernel-owned main or interpreter images. Successful mapping does not publish
an image, resolve symbols, relocate dependencies or run lifecycle functions.

`mapping_test.cpp` injects every acquisition/commit syscall failure and cleanup
failures into the production path. Its low-address native memory model checks
ownership and requested protections; it does not establish target kernel mmap,
mprotect or fault behavior. Real target observations remain integration work.

## Dependency Search

`linux/search.h` separates bounded startup environment admission, candidate
selection and transactional DSO mapping. It reads no host configuration and
uses no public getenv or allocation. Callers supply immutable readable strings
without terminating NUL bytes, absolute origin directories established from
image provenance, and nearest-parent-first ancestry. Origin discovery and the
dependency graph are separate responsibilities; argv[0] is not an origin.

Secure startup is rejected before reading environment strings. LD_LIBRARY_PATH
is the only supported nonempty loader environment setting. Nonempty LD_PRELOAD
and other LD_ settings are rejected; the parser exposes the rejected setting
for diagnostics. Duplicate LD_LIBRARY_PATH entries are rejected, ordinary
non-loader variables are ignored, and empty loader settings have no effect.

A slash-containing expanded name is attempted directly, including an explicitly
relative pathname. Otherwise, a requester without RUNPATH searches its RPATH
and ancestor RPATH entries (excluding ancestors with RUNPATH), followed by
LD_LIBRARY_PATH, the requester's direct-only RUNPATH, /lib and /usr/lib. RUNPATH
presence suppresses inherited RPATH even when empty. Empty colon-separated
elements do not imply the current directory. Explicit nonempty relative
elements remain relative pathnames, not an implicit fallback.

Only $ORIGIN and ${ORIGIN} are expanded, using each path owner's directory;
environment expansion uses the main executable's origin. Unknown tokens,
embedded NUL and paths exceeding 4095 bytes are rejected. Applicable search
elements are preflighted before attempting any candidate. ENOENT/ENOTDIR and
EACCES permit continued search, with EACCES preserved if no candidate succeeds.
Other errors and any cleanup error stop immediately. The production adapter
uses DsoMapping; the candidate callback also exposes the selected spelling to
later graph bookkeeping. No cache, hardware-capability directory or preload
provider is consulted. Native model tests establish this selection/error policy,
not installed target filesystem or startup execution.

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

## Initial Entry

The bounded stack decoder checks argc/argv/envp layout, mandatory auxv values,
duplicates, page size and user-address arithmetic. It rejects AT_SECURE before
any environment string is read. Main-image discovery derives bias from its
PT_PHDR, not AT_BASE. Admission then cross-checks both mapped images, the actual
interpreter PC, main entry, complete tables and readable random bytes, including
rejection of overlapping main/interpreter load pages.

The MMIX entry object captures original SP and its own PC, establishes a normal
bootstrap call window, and preserves SP in the root register window. Its return
path restores SP and jumps to the validated main entry without touching TP or
returning through the kernel's initial rJ. The hidden
`__llvm_libc_loader_bootstrap` continuation takes `(original_sp, entry_pc)` and
performs self-relocation before calling the hidden
`__llvm_libc_loader_after_relocation` continuation. The latter must complete
main-image validation and startup before returning the admitted main entry;
startup failure terminates instead of returning. It is intentionally unresolved
until startup composition is implemented. No test stub is provided as a
production interpreter.

Supplied stack and mapped spans must already be readable. These helpers do not
probe arbitrary addresses or recover from a corrupt kernel pointer. Their host
tests and entry-object inspection do not qualify kernel entry or main transfer.

## Self Relocation

The MMIX bootstrap uses hidden PC-relative references and raw syscall/private
arena helpers. It verifies AT_BASE against the hidden ELF-header address and
the captured entry against the admitted self image. Its own interpreter must
place the complete ELF header and program table in the first readable page;
this is a bootstrap artifact constraint, not a restriction on dependent DSOs.

The shared self relocator admits only symbol-free RELATIVE records, using a
target-owned relocation identity. It checks the complete table, signed
base-plus-addend arithmetic and readable/writable destinations before changing
any byte. Destinations cannot overlap each other or validation metadata.
Unknown dynamic semantics, dependencies, TLS, PLT and initializers are rejected.
Byte-wise stores support unaligned destinations without native pointer casts.
Self RELRO is protected before entering the ordinary hidden continuation.
Temporary views are borrowed until that continuation returns; it must copy any
state needed later before the private arena is released.

`self_relocation_test.cpp` covers malformed metadata and failure atomicity.
`bootstrap_continuation.cpp` is a test-only link fixture with a relocated
pointer, never a production startup implementation. The native
`loader-linked-image-test` accepts a cross-linked fixture pathname, admits its
file/mapped views and checks relocated values without executing target code.
Inspect the final image as well as each bootstrap input object: hidden
visibility alone does not prove that early data access is relocation-free.

## Current Target Support

Only MMIX Linux is currently admitted, using Clang. Its implementation in
`linux/mmix/` contains machine identity admission on decoded ELF headers and a
raw syscall adapter reusing libc's hidden MMIX assembly leaf. Its current Linux
policy selects 8192-byte pages, the 43-bit user-address limit, null-page
exclusion and four-byte entry alignment. Parser and self-relocation components
are built, but dependency relocation and process startup remain incomplete.
Support for other architectures requires their target implementations and
validation.

The intended MMIX interpreter artifact is `ld-llvm-mmix.so.1`. No executable or
install rule is provided until complete startup composition; the current build
does not provide a runnable dynamic loader.
