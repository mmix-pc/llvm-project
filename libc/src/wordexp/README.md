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
* `dollar_quote` distinguishes character escapes from raw numeric bytes and
  validates discarded text after numeric NUL. Encoding belongs to the consumer.
* `pattern` implements parameter prefix/suffix removal through the locale-aware
  matcher, including character counts and stateful candidate encoding.
* `fragments` owns annotated bytes across environment lookups and assignments.
* `expand` joins these components through fallible environment, account,
  arithmetic, command and output services. It preserves expansion origins and
  validates the complete source before invoking services.
* `environment` supplies Linux environment and account lookup services. Account
  buffers are invocation-owned and grow on ERANGE. It does not provide command
  execution or diagnostic output.
* `command` captures raw output from an already admitted command body through
  `/bin/sh`, using the existing spawn provider and `PipeProcess` ownership
  policy. It controls stderr, drains the pipe before waiting, and closes and
  reaps on sink errors or cancellation. It does not parse command boundaries,
  implement NOCMD, or normalize captured bytes.
* `results` stages word-vector ownership, offsets and APPEND transactions;
  `wordfree` releases that representation while preserving errno.

Public integration must retain original source bytes and encoding states;
the scalar-only expansion interface is not yet a byte-preserving source
adapter for state-dependent filenames. IFS decoding, pathname expansion,
command syntax/execution, cancellation ownership and public flags/errors remain
integration requirements. Component
tests do not establish installed public-interface qualification.
