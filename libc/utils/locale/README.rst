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

Numeric Category
----------------

``numeric_category.py`` converts all three required NUMERIC source fields into
category 1 records: decimal_point (key 1, string), thousands_sep (key 2, string),
and grouping (key 3, bytes). Both strings use normalized UTF-8. The decimal
point must be one non-NUL Unicode scalar; the separator may also be empty.
``NumericLocaleData`` applies the same validation independently in the reader,
using the existing UTF-8 decoder rather than the thread's current locale.

Grouping widths are 1 through 126. A trailing zero repeats the last width;
a sole zero means no grouping. The private stop sentinel is 127, permitted
only immediately before the trailing zero. It must be translated to the
target's CHAR_MAX when building a public lconv. Source zero and -1 stop
grouping; neither is confused with the output's repeat-last terminator.
Unsupported input and missing fields fail rather than selecting C defaults.

These records validate only NUMERIC. A file containing them without the other
required category schemas is still not a publishable locale. Strings are not
implicitly converted to the selected LC_CTYPE by this storage component.

Monetary Category
-----------------

``monetary_category.py`` emits category 4. String keys 1 through 6 are
int_curr_symbol, currency_symbol, mon_decimal_point, mon_thousands_sep,
positive_sign and negative_sign. Key 7 contains grouping bytes with the same
private sentinels as NUMERIC. Keys 8 through 21 contain one big-endian word
each: int_frac_digits, frac_digits, p_cs_precedes, p_sep_by_space, n_cs_precedes,
n_sep_by_space, p_sign_posn, n_sign_posn, int_p_cs_precedes, int_p_sep_by_space,
int_n_cs_precedes, int_n_sep_by_space, int_p_sign_posn and int_n_sign_posn.

The producer requires all base fields. Under the selected glibc source profile,
absent international formatting fields inherit the corresponding local fields;
explicit international values take precedence. Every inheritance is recorded.
The private unspecified value 127 must become the target's CHAR_MAX when
constructing a public lconv. No native char representation is serialized.

``MonetaryLocaleData`` independently checks required fields, UTF-8, separator
lengths, grouping and each numeric domain. International currency symbols are
empty or three uppercase ASCII letters followed by one Unicode scalar. This
checks the representation, not membership in a currency registry. The component
does not implement localeconv, strfmon, category encoding conversion or locale
publication. Its non-owning views require retained immutable storage.
