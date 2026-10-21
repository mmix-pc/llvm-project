This directory also contains the Python sources for hdrgen, which is
what generates the headers public libc headers. The definitions for these
headers are in the ``include`` directory. The ``.h.def`` files are the bases
and the ``.yaml`` files are the contents.

Function declarations have ``noexcept: true`` by default and receive the
``__NOEXCEPT`` suffix. Set ``noexcept: false`` for functions that can propagate
an exception, including functions that call potentially throwing callbacks.
The field must be a YAML boolean and applies to guarded declarations as well.

An optional ``public_name`` selects the identifier used in a function
declaration while ``name`` remains the entrypoint and assembly symbol. The
generator emits an assembly-label redirection rather than a function macro,
so taking the function address or suppressing macro expansion preserves the
selected ABI. C and C++ exception specifications retain their required
ordering relative to the assembly label. This facility requires a compiler
supporting GNU-style assembly labels.
