# Word Expansion Components

This directory contains private components for POSIX word expansion. It does
not yet provide the public `wordexp` or `wordfree` entrypoints.

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

The enclosing parser must preserve quoting and expansion origins, validate
prohibited command syntax before command execution, and evaluate parameter
alternatives lazily. Environment access, tilde lookup, IFS decoding, actual
command substitution, pathname expansion, and public result/error ownership
are separate integration requirements. No component invokes a shell.
