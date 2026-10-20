Bounded DNS packet interfaces
============================

These providers construct QUERY packets and parse caller-owned DNS packets.
They do not perform network name resolution. There is no resolver state,
configuration-file access, search-domain processing, cache, ``res_init``,
``_res``, ``res_query``, or extension of ``getaddrinfo`` here. The entrypoints
belong in libc.a and libc.so; no separate libresolv is required.

The wire algorithms implement RFC 1035 sections 3.1, 4.1, and 5.1. Query
entropy uses the Linux ``linux_syscalls::getrandom`` wrapper.

Contracts
---------

* ``ns_get16`` and ``ns_get32`` require two and four readable octets,
  respectively. Their traditional signatures have no bounds argument.
* ``ns_name_uncompress`` checks pointer targets, bounds, cycles, reserved label
  encodings, the 255-octet expanded wire limit, and output capacity. It returns
  the bytes consumed at the original source, not at compression targets.
  Root renders as ``.``. ``dn_expand`` instead renders root as an empty string.
* Presentation output escapes syntax-significant characters and renders
  control, space, and non-ASCII octets as three-digit decimal escapes.
* ``dn_skipname`` and ``ns_skiprr`` cannot validate pointer targets without a
  packet base. They check encoded-name and record boundaries without following
  compression pointers. ``ns_initparse`` validates every owner name instead.
* ``ns_initparse`` checks all four section counts and each record's fixed fields
  and RDLENGTH envelope, and rejects trailing bytes. RDATA remains opaque.
  A successful handle borrows the packet; the caller must keep it alive.
* ``ns_parserr`` supports explicit record indices, backward/random access,
  and ``-1`` for sequential access within the requested section. Changing
  section with ``-1`` starts at record zero. Exhaustion returns ENODEV.
* ``res_mkquery`` accepts QUERY only, class/type values in 0..65535, and null
  data/newrr with zero datalen. It emits QDCOUNT=1, RD=1, all other counts/flags
  zero, and an uncompressed question. It accepts root and absolute names,
  including presentation escapes. Unsupported inputs fail with EINVAL;
  insufficient buffers and oversized names fail with EMSGSIZE.
* The query ID comes only from OS randomness. Short reads and EINTR are retried;
  other errors are reported in errno and zero progress is EIO. There is no
  fallback ID source. Success preserves errno. All failures leave the query
  buffer unchanged. The templated internal constructor admits deterministic
  readers for testing; the production entrypoint always binds OS getrandom.

Integration and tests
---------------------

The source and test CMake files define the providers and tests. Build integration
requires source/test subdirectories, public header/type/macro/proxy targets,
target header and entrypoint lists, and libc archive/shared-library inclusion.
The ns_rr type and resolv macros depend on arpa-nameser-macros.h. Linux query
construction depends on the internal getrandom syscall-wrapper target.

Tests compile namespace entrypoint sources directly so native overlay tests do
not disappear when those entrypoints are absent from the host's default list.
A separate translation unit tests LLVM libc's own public HEADER layout and
macros even in overlay mode. Packet and query tests use deterministic fixtures;
the query core's entropy reader is injected only by tests.
