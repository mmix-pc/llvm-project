# Word Expansion Components

This directory contains components for POSIX word expansion. The public
`wordexp` pipeline is incomplete; `wordfree` has source and ownership tests but
is not yet selected for MMIX exports.

* `parameter` parses operators inside an already delimited `${...}` and selects
  the required action without evaluating its word. Input slices remain borrowed.
* `arithmetic` evaluates already substituted, quote-removed text using explicit
  variable callbacks. Syntax is checked before callbacks run; unselected operands
  have no variable or arithmetic effects. Signed overflow and invalid arithmetic
  return errors. Nesting is bounded to protect the stack. This is not a shell
  parser or an environment-variable expression evaluator.
* `fields` splits annotated expansion results using decoded IFS delimiters.
  Literal and quoted portions are not split, expansion origins are not merged,
  and quoted empty fields survive. The caller owns output through fallible sink
  callbacks, including partial output on failure.
* `syntax` validates decoded source before emitting tokens. Parameter operands
  retain their quote boundaries and are evaluated only when selected. Command
  boundaries require a separate, side-effect-free shell syntax service.
* `shell_syntax` parses command-substitution compound lists, nested commands
  and here-documents without invoking expansion services. It retains original
  positions for backquote diagnostics. Numeric-dollar-quoted document delimiters
  still require the encoding adapter and fail explicitly until integrated.
* `source` owns original bytes, scalar input and per-character encoding states.
  It preserves shift prefixes and suffixes rather than reconstructing byte
  offsets by re-encoding. Failed replacement retains the old snapshot.
* `dollar_quote` distinguishes character escapes from raw numeric bytes and
  validates discarded text after numeric NUL. Encoding belongs to the consumer.
* `pattern` implements parameter prefix/suffix removal through the locale-aware
  matcher, including character counts and stateful candidate encoding.
* `fragments` owns annotated bytes across environment lookups and assignments.
* `expand` joins these components through fallible environment, account,
  arithmetic, command and output services. It preserves expansion origins and
  validates the complete source before invoking services. Command output drops
  embedded NUL bytes, preserves subsequent bytes, then strips trailing newlines.
* `environment` supplies Linux environment and account lookup services. Account
  buffers are invocation-owned and grow on ERANGE. It does not provide command
  execution or diagnostic output.
* `command` captures raw output from an already admitted command body through
  `/bin/sh`, using the existing spawn provider and `PipeProcess` ownership
  policy. It controls stderr, drains the pipe before waiting, and closes and
  reaps on sink errors or cancellation. It does not parse command boundaries,
  implement NOCMD, or normalize captured bytes.
* `ifs` owns decoded delimiter slices and distinguishes unset and empty IFS.
  It retains original byte sequences rather than re-encoding delimiters;
  expansion results themselves may contain arbitrary non-NUL bytes. Additional
  whitespace follows the caller's retained LC_CTYPE selection; unset IFS still
  uses only space, tab and newline.
* `paths` joins field splitting, quote-aware glob patterns and owned results.
  It preserves original bytes for literal or unmatched fields, and does not
  treat backslashes introduced by expansions as new shell quoting syntax.
* `results` stages word-vector ownership, offsets and APPEND transactions;
  `wordfree` releases that representation while preserving errno.

Public integration must join source byte slices with their encoding states;
the scalar-only expansion interface does not yet compose the source snapshot
for state-dependent filenames. IFS and pathname-provider integration,
remaining command syntax and capture integration, cancellation ownership of the complete
pipeline and public flags/errors remain integration requirements. Component
tests do not establish installed public-interface qualification.
