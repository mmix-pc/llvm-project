# Word Expansion Components

This directory contains components for POSIX word expansion. The public
`wordexp` pipeline is incomplete. The `wordexp` and `wordfree` entrypoint sources
have focused tests but are not yet selected for MMIX exports.

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
* `encoded_fields` matches state-dependent IFS by decoded character, not by
  payload bytes. It retains expansion boundaries and original wire spellings,
  restoring only required shift state after removed delimiters. Shift-only
  tails cannot fabricate fields. Invalid stateful input requiring character
  interpretation fails explicitly; stateless byte splitting remains unchanged.
* `syntax` validates decoded source before emitting tokens. Parameter operands
  retain their quote boundaries and are evaluated only when selected. Command
  boundaries require a separate, side-effect-free shell syntax service.
* `shell_syntax` parses command-substitution compound lists, nested commands
  and here-documents without invoking expansion services. It retains original
  positions for backquote diagnostics. Its encoding-aware service admits
  numeric-dollar-quoted document delimiters in stateless encodings. Stateful
  here-documents still require original-byte comparison and fail explicitly.
* `source` owns original bytes, scalar input and per-character encoding states.
  It preserves shift prefixes and suffixes rather than reconstructing byte
  offsets by re-encoding. Failed replacement retains the old snapshot. Command
  preparation restores a saved boundary and removes one legacy escape layer,
  preserving designation bytes attached to removed characters.
* `dollar_quote` distinguishes character escapes from raw numeric bytes and
  validates discarded text after numeric NUL. Encoding belongs to the consumer.
* `quote` composes character and numeric escapes using saved conversion state.
  Literal characters retain their source spelling; raw bytes are not silently
  repaired. Adjacent quote spans can continue a partial numeric sequence.
* `pattern` implements parameter prefix/suffix removal through the locale-aware
  matcher, including character counts and stateful candidate encoding.
* `fragments` owns annotated bytes across environment lookups and assignments.
* `expand` joins these components through fallible environment, account,
  arithmetic, command and output services. It preserves expansion origins and
  validates the complete source before invoking services. Command output drops
  embedded NUL bytes, preserves subsequent bytes, then strips trailing newlines.
  The owned-source entry retains absolute offsets through selected operands and
  uses state transitions for ordinary literal spans without re-encoding them.
* `environment` supplies Linux environment and account lookup services. Account
  buffers are invocation-owned and grow on ERANGE. It does not provide command
  execution or diagnostic output.
* `command` captures raw output from an already admitted command body through
  `/bin/sh`, using the existing spawn provider and `PipeProcess` ownership
  policy. It controls stderr, drains the pipe before waiting, and closes and
  reaps on sink errors or cancellation. It does not parse command boundaries,
  implement NOCMD, or normalize captured bytes. Its source-token overload joins
  checked source preparation to capture before acquiring process resources.
* `ifs` owns decoded delimiter slices and distinguishes unset and empty IFS.
  It retains original byte sequences rather than re-encoding delimiters;
  expansion results themselves may contain arbitrary non-NUL bytes. Additional
  whitespace follows the caller's retained LC_CTYPE selection; unset IFS still
  uses only space, tab and newline.
* `paths` joins field splitting, quote-aware glob patterns and owned results.
  It preserves original bytes for literal or unmatched fields, and does not
  treat backslashes introduced by expansions as new shell quoting syntax.
* `results` stages word-vector ownership, offsets and APPEND transactions;
  its flag transaction handles REUSE and publishes partial resource-failure
  results, while other failures retain previous APPEND results. `wordfree`
  releases that representation while preserving errno.
* `evaluate` joins source admission, expansion, IFS, pathname matching and result
  publication. It returns a WRDE status separately from an underlying error.
* `runtime` supplies real Linux environment, account, shell capture and stderr
  services. Completed commands contribute output even after a nonzero exit.
* `wordexp` selects the current locale and translates the private outcome to
  the public return value and errno. Resource failures map to WRDE_NOSPACE;
  other service failures map to WRDE_SYNTAX with their actual errno. The latter
  is an explicit extension, not a claim that I/O errors are syntax errors.

Public integration still needs stateful here-document byte comparison,
shift-only source suffix handling, complete stateful IFS/path joins, and the
full installed cancellation and ownership checks. Ordinary literal and quoted
transitions do not qualify every state-dependent filename or raw-byte
combination. Component and source-overlay tests do not establish installed
public-interface qualification.
