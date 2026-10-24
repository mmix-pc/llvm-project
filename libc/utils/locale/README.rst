Private Locale Data Container
=============================

``locale_file.py`` encodes normalized category records for the internal
``LocaleDataView`` reader. It is not a localedef implementation, a parser for
glibc locale sources, or evidence that the records implement a locale.
Semantic category generation and validation must precede runtime publication.
This unpublished format has no cross-version compatibility guarantee.

All integers are unsigned, 32-bit, big-endian. Offsets and lengths count bytes;
no alignment is required. The maximum file size is 64 MiB.

The 32-byte header contains the eight-byte magic ``LLVMLCL\0``, version 1,
endianness marker 0x01020304, total file size, category count 6, name offset 128
and name length including its final NUL. The name uses ASCII letters, digits,
underscore, dot, hyphen or @, occupies 2 through 256 bytes, and is neither
``.`` nor ``..``. It is an identity, not authorization to open a pathname.

Six 16-byte directory entries follow, ordered by category ID: CTYPE, NUMERIC,
TIME, COLLATE, MONETARY, MESSAGES (0 through 5). Each contains its ID, absolute
section offset, section length and item count. Sections cannot overlap the
header, directory, name or previous sections. An empty section is structurally
valid; category-specific validation must reject missing required data.

Each section begins with its item table. Entries contain a nonzero key, kind,
section-relative payload offset and payload length. Keys are strictly
increasing. Payloads cannot overlap the table or previous payloads. Kinds are:

* 1: NUL-terminated string with no interior NUL. The normalized producer uses
  UTF-8; this does not establish the runtime encoding of a locale category.
* 2: Array of big-endian 32-bit words.
* 3: Uninterpreted bytes, including grouping or other explicitly typed data.
* 4: Three words naming a category, item key and terminal kind (1 through 3).
  The referenced item must exist with that kind. Reference chains and cycles
  are forbidden, including references within a single category.

The container itself does not assign semantic item keys. Category readers
define required fields and validate Unicode, grouping and collation semantics.
Structural validation alone must never publish a usable
locale. The non-owning view requires immutable retained input; the owned
``LocaleDataFile`` copies and validates input before returning a reference.

The producer accepts JSON with ``name`` and a six-element ``categories`` list.
Each category is a list of objects containing ``key``, ``kind`` and ``value``.
Kinds are ``string``, ``words``, ``bytes`` and ``reference``. A reference value
is ``[category, key, terminal_kind]``. Unknown members, duplicate keys and
duplicate JSON members are errors. Output order is independent of input item
order. The command refuses to overwrite an existing output:

.. code-block:: sh

   python3 locale_file.py normalized.json locale.dat
   python3 -B -m unittest discover -s . -p test_locale_file.py

Normalized input is not a substitute for source provenance, licenses, category
dependency closure, or independent semantic oracles. No external data is
installed by this tool, and built-in C/POSIX and C.UTF-8 do not require it.

Source Normalization
--------------------

``simple_categories.py`` reads the NUMERIC, MONETARY, TIME and MESSAGES
sections of the selected source profile. It handles explicit comment/escape
characters, continued lines, literal UTF-8, Unicode symbols and typed field
lists. It rejects unknown selected directives, duplicate fields, unsupported
symbol names and malformed structure. It does not infer a numeric value from
a quoted string or discard unsupported selected syntax.

The output records the source hash, original preamble and explicitly unparsed
categories. It is marked non-publishable: character/collation/transliteration
semantics, field defaults, category-specific value validation and conversion to
the runtime schema remain required. In particular, recognizing a category
without every mandatory field does not qualify that category. This bounded
normalizer does not implement a general localedef input language.

.. code-block:: sh

   python3 simple_categories.py /readonly/locales/de_DE de_DE-review.json
   python3 -B -m unittest discover -s . -p 'test_*.py'
