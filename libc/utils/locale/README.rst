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

Character Source Profile
------------------------

``ctype_source.py`` expands category-scoped copy and transliteration include
dependencies from an explicit source directory. Source names cannot escape
that directory. The graph, input bytes and rule counts are bounded; missing
files, cycles, unsupported directives and malformed scalar ranges fail. Each
source retains its hash and original preamble. Classification intervals, case
pairs and ordered transliteration alternatives have separate normalized forms.
Local case entries override inherited entries without dropping unrelated pairs;
class entries add membership. Local transliteration rules override imports;
alternative order and empty replacements are preserved.

The selected profile accepts Unicode-symbol and literal UTF-8 sequences,
including Turkish case tailoring and German transliteration alternatives.
It does not validate collation, supply a codec, resolve recursive transliteration
at runtime, or approve every input's redistribution. Output remains explicitly
non-publishable until the full data and runtime pipeline is qualified.

``character_records`` emits category 0 classification and case tables as
big-endian word pairs. Class keys 1 through 11 are upper, lower, alpha, digit,
space, cntrl, punct, graph, print, xdigit and blank. Each pair is an inclusive
scalar interval. Keys 0x101 through 0x103 are toupper, tolower and totitle;
pairs are source/destination scalars. Missing mappings are identities. Named
extension classes and transliteration are retained in normalization but not
silently placed in these standard tables.

``CTypeLocaleData`` validates sorted, non-overlapping scalar intervals, sorted
unique map keys and required subset/disjoint relationships between classes.
Lookup is binary and does not cast unaligned serialized words. This is a
character-table component, not a complete LC_CTYPE: codec identity, encoding
tables, transliteration execution, byte-domain policy and source-profile
completeness still require their owning runtime checks.

.. code-block:: sh

   python3 ctype_source.py /readonly/locales tr_TR tr_TR-ctype-review.json

Time Text Tables
----------------

``time_category.py`` emits category 2 tables. Field IDs 1 through 16 are abday,
day, abmon, mon, alt_mon, ab_alt_mon, am_pm, d_t_fmt, d_fmt, t_fmt, t_fmt_ampm,
era, era_d_fmt, era_t_fmt, era_d_t_fmt and alt_digits. Each field's key
``id << 16`` holds one word containing its count. Following keys contain
individual UTF-8 strings. Day tables have seven entries, month tables twelve,
am_pm two and individual formats one. Era entries are bounded to 1024;
alternative digits are bounded to 100. Empty names are rejected, but empty
AM/PM and format strings are preserved.

The selected glibc profile inherits missing alternate months from the ordinary
month table. Missing eras, era formats and alternative digits have explicitly
recorded empty defaults. Other required fields cannot be omitted. GNU source
extensions remain in the review output, not silently exported as standard
items. ``TimeLocaleData`` validates cardinality, item kinds and UTF-8, and
provides checked access without assuming aligned storage. This component does
not validate era calendars or interpret format programs; those checks must
precede runtime locale publication. No private data version compatibility is
promised while this schema is under development.

Message Text
------------

``messages_category.py`` emits category 5 keys 1 through 4: yesexpr, noexpr,
yesstr and nostr. Every item is a UTF-8 string. The expressions are required;
absent legacy response strings become explicitly recorded empty values.
``MessagesLocaleData`` checks their presence, kinds and text encoding without
interpreting the expressions. Locale-aware ERE validation must succeed before
publication; Python regular expressions cannot substitute for it. These records
do not implement message catalogs or translation-domain bindings.

Chinese Encoding Maps
---------------------

``encoding_source.py`` reads the selected GB2312 charmap and ICU EUC-TW mapping
source. It retains notices, source hashes and excluded mapping records. Only
CNS planes 1 and 2 participate in ISO-2022-CN. ICU flag 0 contributes exact
encoding and decoding; flag 3 contributes decoding only. Other flags remain
explicit non-exact review records, not silently inverted encoder entries.
Duplicate coordinates or ambiguous exact encodings are rejected.

Category 0 keys 0x201 through 0x206 contain alternating decode/encode word-pair
tables for GB2312, CNS plane 1 and CNS plane 2. Decode keys are 7-bit row/column
coordinates; values are Unicode scalars. Encode pairs reverse these domains,
but are not constructed by blindly reversing the decoder. Every table is sorted
by its lookup key and bounded by the 94-by-94 repertoire. The runtime reader
checks scalar/coordinate domains, ordering and exact encoder round trips.
Input bytes are read without alignment or native-endian assumptions.

The internal ISO-2022-CN state machine consumes one byte at a time and reports
incomplete prefixes, complete shifts or scalars. It rejects undesignated SO/SS2,
retains G1 across SS2 and SI, and clears cached designations on LF or a complete
NUL. Encoding prefers the current exact G1 mapping, then GB2312/CNS1/CNS2.
Output and reset commit only when the entire result fits. The largest scalar
output is eight bytes. Public iconv suffix policy, descriptor ownership and
restartable mbstate_t integration are separate responsibilities; these helpers
alone do not expose the encoding or change MB_LEN_MAX.

Transliteration Records
----------------------

``transliteration_category.py`` serializes the normalized LC_CTYPE rules without
changing alternative order or resolving them against a destination encoding.
Category 0 key 0x300 contains the rule count. Keys starting at 0x10000 contain
word records in lexicographic Unicode-scalar source order: source length,
source scalars, alternative count, and each alternative's length and scalars.
Empty alternatives represent deletion. Source sequences are nonempty and
NUL-free. Each sequence and alternative list is bounded to 256 items, the rule
count to 65536 and the total payload to 4M words. The reader checks every bound,
scalar, key order and exact record length before permitting lookup. Source
``default_missing`` stays in review metadata rather than silently overriding
the conversion fallback policy.

The internal converter commits complete characters or shifts, leaves incomplete
input unconsumed, and stages complete output before changing destination state.
Default nonidentical conversion emits ``?``; discard and ignore are distinct
policies. Ignore searches at source-code-unit boundaries for a complete valid
successor and never applies invalid shift bytes. A trailing invalid run without
such a successor remains an error. Reset with an output span emits any required
SI atomically; reset without output clears the state without emission.

Transliteration rules are supplied for each call, not captured by the converter.
The eventual locale integration must retain the calling thread's selected rules
for that call. The first complete matching source key and first entirely
representable alternative are selected; failed alternatives publish neither
bytes nor shift-state changes. Missing rules fall back to ``?``. All admitted
regional sources currently use single-scalar keys; multi-scalar records have
separate constructed tests. Descriptor allocation, file discovery, public
iconv names/indicators and current-locale acquisition remain separate integration
work. These components do not alone expose a public converter or install a
complete locale. The internal name selector accepts ASCII (also US-ASCII),
ISO-8859-1, UTF-8, UTF-16LE/BE, UTF-32LE/BE and ISO-2022-CN, with ASCII-insensitive
case matching. It accepts one destination indicator, rejects source indicators
and combinations, and does not guess the byte order of UTF-16 or UTF-32. Empty
names are left for the public locale-aware layer to resolve.

``EncodingDescriptor`` adds stable-address converter storage and retains the
immutable mapping file until the last descriptor releases it. The factory
validates names and tables before publication and rolls back allocation failure.
Stateless encodings require no table file. Descriptors have independent mutable
shift state; rules are still borrowed per call. File discovery, current-locale
acquisition and public iconv entrypoints remain separate integration work.

Collation Source And Keys
-------------------------

``collation_source.py`` reads the selected four-level source profile, including
category copy dependencies, abstract symbols, explicit symbol/character ranges,
multi-character elements, expansions, IGNORE and regional reordering. Unknown
active directives, missing weights, cycles and duplicate sequence keys fail.
Files, symbols, records and sequence sizes are bounded. The review preserves
source hashes, dependency edges and notices; this is not a general localedef.

The selected sources omit UNDEFINED. The producer warns and uses the POSIX
default: unlisted scalars sort after explicit entries, share a primary weight
and have codepoint-ordered subsequent weights. This is not glibc's observed
fallback behavior. Explicit source weights and regional reordering are retained.

``collation_category.py`` emits category 3. Key 1 contains the record count,
maximum explicit weight, implicit-policy ID 1 and four fallback direction flags.
Keys starting at 0x1000 hold records in scalar-sequence order: source length,
source scalars, four direction flags, then each level's weight count and weights.
Direction 1 is forward, 2 backward and bit 4 enables position sensitivity.
The position flag is uniform within each level. Empty weight lists mean IGNORE.
The reader bounds all counts, validates scalar/weight domains and requires exact
record consumption. Implicit rules need no table enumerating unlisted scalars.

``collation_key`` resolves longest elements into caller-owned scratch storage.
Comparison and transformation use the same four-pass token stream. Backward
sections reverse elements without reversing expansions. Position levels count
ignored elements. The order-preserving key has no embedded NUL; byte and wide
outputs each count their own elements. A short output returns the full required
length without writing beyond capacity. No allocation, locale selection or
public strcoll/wcscoll qualification follows from this internal component.

Regional Composition
--------------------

``regional_locale.py`` composes all six categories for the selected en_US,
de_DE and tr_TR UTF-8 profiles. It records the encoding name at CTYPE key 0x200,
retains transliteration alternatives and collects unchanged source files with
their notices alongside locale.dat and review.json. It verifies source hashes
again while collecting them and refuses an existing destination. The output is
an explicit staging package, not an installation into the runtime search path.

.. code-block:: sh

   python3 regional_locale.py --source-root /readonly/locales \
     --locale tr_TR --output /staging/tr_TR.UTF-8

The review remains runtime-unqualified. In particular, time-format and era
semantics, locale-aware message expressions, category ownership/publication and
installed consumers still require their runtime acceptance. Six structurally
valid categories must not be advertised as a usable regional locale.

Embedded Transliteration And Public Conversion
---------------------------------------------

``builtin_transliteration.py`` merges ``translit_neutral`` followed by
``translit_combining`` into the built-in read-only table. Its JSON companion
retains every source hash, original preamble and dependency edge. Regenerate
from the reviewed source revision; ``--check`` compares both generated files
without modifying them:

.. code-block:: sh

   python3 builtin_transliteration.py --source-root /readonly/locales \
     --output ../../src/locale/builtin_transliteration --check

The runtime validates the table when acquiring a TRANSLIT descriptor, then
borrows the immutable bytes. Built-in C/POSIX and C.UTF-8 require no data file
for transliteration. Regional rules are borrowed from the current LC_CTYPE on
each conversion call, not captured at descriptor creation. Application
synchronization of global locale changes remains required.

``encoding_package.py`` stages the separate, controlled Chinese mapping file
and preserves the complete source files and normalized review:

.. code-block:: sh

   python3 encoding_package.py --gb2312 /readonly/GB2312 \
     --cns /readonly/euc-tw-2014.ucm --output /staging/chinese

Its ``ISO-2022-CN.dat`` is deployed under ``encodings/`` in the configured
private locale data root. The file identity must be ``ISO-2022-CN``. The
runtime validates the file and owns a frozen copy before publishing a
descriptor. It never interprets application encoding names as paths. Missing
files do not affect stateless encoding pairs, and already-open descriptors
retain their data when the file name becomes unavailable. These generators
do not perform installation or establish installed-runtime qualification.

Data Bundle Staging
-------------------

``data_bundle.py`` assembles the three generated regional packages and the
separate encoding package into a fresh target tree. It verifies container
names, binary/source hashes and producer identities, and requires both pinned
Unicode and ICU license texts. Original source files, notices and reviews are
preserved under ``.provenance`` within the private data root. A manifest records
every delivered file's target-relative path, size and hash, without a host
installation prefix. It does not infer semantic qualification from these checks.

.. code-block:: sh

   python3 data_bundle.py \
     --regional /staging/en_US.UTF-8 --regional /staging/de_DE.UTF-8 \
     --regional /staging/tr_TR.UTF-8 --encoding /staging/chinese \
     --unicode-license /reviewed/unicode-license.txt \
     --unicode-license-sha256 "$UNICODE_LICENSE_SHA256" \
     --icu-license /reviewed/icu-LICENSE \
     --icu-license-sha256 "$ICU_LICENSE_SHA256" \
     --output /staging/locale-component

The default target root is ``/usr/share/llvm-libc/locale``. ``--data-root`` must
match the runtime's ``LIBC_LOCALE_DATA_ROOT`` when using a different root. The
output is a component staging tree, not an instruction to overwrite an existing
sysroot. Packaging fails before publication for missing or changed inputs;
copy failure removes only its newly created output. Existing outputs are never
replaced. Installation tooling must merge the verified component and preserve
its manifest alongside other component identities.

The stateful validation locale is deliberately not selected. C/POSIX and
C.UTF-8 remain built in. This tool does not install gettext application
catalogs, generate a target localedef utility or mark any runtime test passed.

Display Widths
--------------

``width_table.py`` imports only the selected UTF-8 charmap's WIDTH section,
rejecting overlaps, invalid scalar ranges and unsupported widths. Its companion
manifest records the complete source hash and the original section's rules and
Unicode version independently of classification/collation data versions.

.. code-block:: sh

   python3 width_table.py --source /readonly/charmaps/UTF-8 \
     --output ../../src/wchar/width_table --check

The currently selected UTF-8 profiles share these immutable widths. Runtime
queries first check printability in the selected LC_CTYPE, with NUL handled
separately; C/POSIX therefore does not acquire Unicode classification from the
width table. Printable characters omitted from WIDTH have width one.
``wcswidth`` sums character widths, not bytes or rendered grapheme clusters,
and reports EOVERFLOW rather than overflowing the returned int.

Stateful Validation Locale
--------------------------

``validation_locale.py`` builds ``llvm-test.ISO-2022-CN`` with the reviewed
GB2312/CNS mappings and the selected ``i18n`` classification, case-mapping and
transliteration sources. Other categories explicitly inherit C through the
private string record 0x7fff0001. The runtime rejects an absent or unknown
inheritance value. The ordinary locale loader, category ownership and
restartable converter consume the package; it contains no alternate test codec.

.. code-block:: sh

   python3 validation_locale.py --source-root /readonly/locales \
     --gb2312 /readonly/GB2312 --cns /readonly/euc-tw-2014.ucm \
     --output /staging/llvm-test.ISO-2022-CN

Deploy ``locale.dat`` only to isolated validation roots under
``llvm-test.ISO-2022-CN/``. No default installation target selects this package.
It is not zh_CN/zh_TW data or a regional Chinese locale. Its review records
unmodified source notices and hashes and does not itself qualify runtime APIs.
