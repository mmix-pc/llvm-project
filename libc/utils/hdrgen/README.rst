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

Functions, types, objects and macros accept an optional ``guard`` naming a
preprocessor identifier. It tests whether the identifier is defined, not its
numeric value. On macros with ``macro_header``, the guard controls the header
import; otherwise it controls the definition. Explicit type metadata takes
precedence over types inferred from function signatures. A guarded compiler
type still imports its compiler header using angle brackets.

Merged declarations with different guards are retained. Identical declarations
are deduplicated; conflicting declarations with the same name and guard are
rejected rather than selecting one by input order. Policy must ensure that
incompatible alternative declarations have mutually exclusive guards.

``--visibility FILE`` optionally applies a separate YAML namespace policy.
The top-level keys are header names. Each header maps ``functions``, ``types``,
``macros`` or ``objects`` to mappings of symbol names to guard identifiers.
The policy is applied after merging and before entrypoint filtering. Unknown
symbols, invalid guards and attempts to replace an existing, different guard
are rejected. The policy file is recorded as a build dependency. This lets
target configuration select visibility without embedding target or standards
policy in the generator. For example::

  example.h:
    objects:
      extension_state: ENABLE_EXTENSIONS
