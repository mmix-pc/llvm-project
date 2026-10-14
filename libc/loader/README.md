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
The target links the production interpreter, without CRT or public runtime
providers. Install only its explicit `libc-loader` component into an isolated
target root:

```sh
cmake --build "$RUNTIME_BUILD" --target libc-loader --parallel 4
cmake --install "$RUNTIME_BUILD" --component libc-loader --prefix "$TARGET_ROOT"
```

This installs `lib/ld-llvm-mmix.so.1`; it does not install tools, a shared libc,
or the freestanding test entry as a public CRT.

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
rejects TLSDESC, version, IFUNC, GNU_UNIQUE, PLT and unknown semantics.
It does not resolve symbols across images or call lifecycle functions.

Main/DSO TLS admission preserves one checked PT_TLS header, including empty
templates, alignment residue and file/memory extents. Defined TLS symbols are
module-relative offsets bounded by that image; imported TLS symbols need no
local template. TLS runtime records require aligned writable storage, matching
symbol types and valid current-module forms. The interpreter remains no-TLS.
`DynamicInfo::requires_tls()` includes PT_TLS, TLS symbols/records and
DF_STATIC_TLS. Startup symbol-scope admission still rejects these images until
layout, relocation and TP adoption are implemented; metadata admission does
not enable TLS execution.

`dynamic_test.cpp` exercises sectionless synthetic inputs and malformed
metadata. `loader-linked-image-test` also parses actual linked fixtures with
section metadata removed and rejects mutations of their hash, string, symbol
and relocation tables; its optional `shared` argument selects the DSO fixture.
`loader-tls-test` covers TLS metadata and activation rejection. Optional file
arguments select real linked TLS DSOs for sectionless parsing and malformed
PT_TLS checks on the host.

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
without terminating NUL bytes, absolute origin directories when known, and
nearest-parent-first ancestry. Origin discovery and the
dependency graph are separate responsibilities; argv[0] is not an origin.
An unknown origin disables only search elements containing ORIGIN; other
directories remain eligible. Direct dependency names requiring an unknown
ORIGIN fail without trying a different pathname. Unknown tokens remain errors,
including tokens after an unavailable ORIGIN in the same element.

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

## Startup Dependency Graph

`linux/graph.h` borrows the admitted kernel main image and constructs a
main-first breadth-first scope. Main file identity and absolute pathname are
independently optional; callers retain its immutable mapped metadata and
supply an admitted environment with the matching main origin. This component
does not infer kernel file provenance from ELF headers or argv[0]. An unknown
main identity never participates in file-identity deduplication. Consequently,
an alias of the main executable cannot be recognized that way; a candidate
still must pass DSO admission, with no guessed identity or SONAME substitution.

Each candidate is opened once, identified with raw statx(AT_EMPTY_PATH), and
mapped through transfer of that same fd. Identity is the device major/minor
and inode tuple; regular-file type and returned inode validity are required.
Already discovered identities retain their first path and parent without a
second mapping. Equal SONAME values never merge distinct files. Dependency
edges preserve NEEDED order and repeats, including cycles back to the main
image, while first-discovery parent links provide acyclic RPATH ancestry.
Relative candidates are made absolute using raw getcwd before opening;
the absolute load spelling, not a symlink-target canonicalization, owns ORIGIN.
Startup is serialized and the working directory must not change during loading.

All nodes, paths, edges and ancestry storage use the private arena. Owned DSO
mappings are distinct from the borrowed main image. Failed discovery invalidates
scope access, releases every owned DSO and retains any failed cleanup obligation
with its descriptors for retry. Descriptor storage is released only after all
DSO cleanup succeeds. As with the underlying owners, reset is explicit. Only a
completed graph exposes its main-first scope. No dependency relocation, code,
initializer, finalizer or externally published loader state is involved.

`graph_test.cpp` executes the real search, graph and mapping components against
a native syscall/filesystem model. It covers diamonds, cycles, path aliases,
same-SONAME distinct files, device-sensitive identity, failed acquisitions and
cleanup. It does not qualify actual target statx, getcwd, mapping or startup.

## Startup Symbol Binding

`StartupSymbols` borrows a completed graph's immutable main-first BFS scope.
Lookup uses validated GNU hashes when present, otherwise SysV hashes. The first
eligible definition wins, including a weak definition before a later strong
one. Default-visible references are preemptible; local, hidden, internal and
protected definitions bind internally. Protected definitions remain exported.
Ordinary function addresses identify the definition directly, without a PLT.

Regular definitions retain checked LOAD extents and executable function
addresses; SHN_ABS values are unbiased constants. Missing weak references yield
zero, distinguished from an absolute definition of zero. All symbolic relocation
references can be checked before writes or initialization. Unreferenced undefined
dynsym entries do not create startup obligations. The graph and descriptor arena
must outlive the resolver. This is not a public dlsym or handle interface.

Native binding tests and linked MMIX DSO inspection cover these rules without
executing target instructions. Startup composition remains separate work.

## Eager Startup Relocation

`relocate_startup` accepts a completed main-first scope with matching writable
mapped views. It revalidates dynamic metadata, then preflights all destinations,
bindings and values across the entire scope before writing. Metadata and objects
must remain immutable and nonaliasing during this serialized operation. Writable
views must identify the exact admitted storage, not an unrelated buffer.

MMIX RELATIVE computes B+A; GLOB_DAT and ordinary 64-bit pointers compute S+A.
Signed addend arithmetic is checked without wrapping, then constrained to the
user address range. Symbolic null pointers are allowed, including unresolved
weak references; RELATIVE values must be valid user addresses. Every destination
is an aligned eight-byte writable non-executable word, disjoint from metadata
and other destinations. All relative writes precede all symbolic writes, using
big-endian bytes. Unsupported link-time/TLS records are errors.

No dependency code executes on either success or failure. Initialization,
production startup composition and guest execution are separate
steps. `StartupGraph::relocate` composes the admitted image/storage views and
records successful relocation before allowing final protection. Native tests
exercise failure atomicity and arithmetic; linked MMIX DSO
fixtures verify actual relocation contents without executing target instructions.

## Final Image Protection

`finalize_image_protections` rechecks all borrowed mapped layouts before the
first syscall. It applies R/RX/RW permissions to each page-disjoint LOAD, then
read-only permissions to page-bounded RELRO. Holes are never included in a
transition; executable stacks and W+X LOADs are rejected during admission.
The initial NX stack remains the kernel's responsibility, not a guessed range.
Call this once after relocation with immutable nonaliasing images, before any
publication or user execution. A syscall failure may leave partial protection;
it is fatal to startup, not permission to continue or retry with weaker access.

The interpreter bootstrap uses this helper before its hidden continuation.
`StartupGraph::finalize_protections` handles main/dependency protection, becomes
idempotent on success, and invalidates the graph on failure. It releases only
owned DSOs, preserves borrowed main mappings, and reports failed cleanup while
retaining ownership for explicit reset retry. Callers must have completed
relocation first through the graph's checked relocation transition.

Native syscall injection checks every transition and cleanup failures. Separate
native subprocesses exercise real mprotect permissions with exact fault-address
and access-error checks. These do not qualify MMIX Linux guest enforcement.

## Startup Initialization

`StartupInitializers::prepare` requires a relocated, protected graph. It computes
dependency-first strongly connected components without recursive C calls and
orders each component by BFS discovery. Edges back to main do not move main
initialization ahead of dependencies. Main PREINIT_ARRAY runs first; each
dependency then runs INIT followed by its INIT_ARRAY, and main runs last.
Array order is preserved. Null and -1 array entries are rejected, not treated
as legacy .ctors sentinels.

All relocated callback addresses are checked for instruction alignment and
executable four-byte coverage in the complete scope, then copied into private
arena storage before any callback executes. Cross-image function pointers are
allowed. The production invoker uses ordinary function pointers and passes
argc/argv/envp from the decoded initial stack. Native trace tests use an internal
invoker instead; they do not execute MMIX callbacks.

Repeated initialization cannot call callbacks twice, and recursive entry or
graph reset during a callback is rejected. The plan retains completed objects
in initialization order for later reverse finalization; it does not run FINI
or export exit/dlopen services. Keep the graph and plan arena alive throughout
their use, and release plan storage explicitly after use. Callbacks must return
normally; shared-libc TLS adoption, recursive loading and escaping exceptions
are outside this freestanding subset. Effects after the first callback cannot
be rolled back.

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
startup failure terminates instead of returning. The production continuation
retains its private startup state in BSS-backed process-lifetime storage.
It does not use the test continuation as an interpreter implementation.
After all bootstrap windows return, entry restores SP and clears rL before GO;
no extra call window or public service argument is introduced.

`linux/startup.h` owns main-image views, dependency state and the initializer
plan. It validates the actual AT_PHDR mapping, derives the ET_EXEC/PIE bias,
checks entry identity, parses environment strings and optionally opens `/proc/self/exe`.
File identity and the `/proc/self/fd/<fd>` pathname refer to the same descriptor;
argv[0] and cwd are never substitutes for executable provenance. Missing procfs
or failed metadata queries leave the corresponding identity/path unknown.
Deleted or unrepresentable paths likewise leave origin unknown; they do not
invalidate the kernel-mapped executable. Failure to close an acquired fd is
still terminal and never causes a retry of that consumed descriptor.
Main and interpreter mappings stay kernel-owned. Preparation failures release
owned graph and arena resources without calling initializers. Bootstrap scratch
storage is released before any callback; failed cleanup is terminal.

The no-TLS handoff preserves TP=0 and rG. Shared-libc adoption and a private
loader-services hook remain future work. The separate `handoff.S`/`handoff.c`
fixture checks entry registers, argc/argv/envp, AT_ENTRY and imported data/calls,
then exits with a raw syscall. Building it is not evidence of guest execution.

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
Temporary self views are borrowed until startup preparation finishes. The
continuation releases the bootstrap arena before calling initializers; retained
main/graph state belongs to a different process-lifetime arena.

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
exclusion and four-byte entry alignment. The production no-TLS startup path
composes parsing, mapping, relocation, protection and initializers. Host-model
tests and artifact inspection do not establish successful target execution.
Support for other architectures requires their target implementations and
validation.

The MMIX interpreter artifact is `ld-llvm-mmix.so.1`. Its explicit install
component is experimental: TLS, shared libc, public dynamic-loading services,
unload and finalization are not supplied by this no-TLS profile.
