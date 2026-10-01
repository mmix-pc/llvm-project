This directory also contains the Python sources for hdrgen, which is
what generates the headers public libc headers. The definitions for these
headers are in the ``include`` directory. The ``.h.def`` files are the bases
and the ``.yaml`` files are the contents.

Function declarations have ``noexcept: true`` by default and receive the
``__NOEXCEPT`` suffix. Set ``noexcept: false`` for functions that can propagate
an exception, including functions that call potentially throwing callbacks.
The field must be a YAML boolean and applies to guarded declarations as well.
