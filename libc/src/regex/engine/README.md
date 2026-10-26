# Libc BSD Regex Derivative

This directory is a private derivative of the Henry Spencer/OpenBSD engine
retained in LLVM Support. Its initial source snapshot is
`llvm/lib/Support` at LLVM repository commit
`e59bca866a40d2dfcec6141afef33c4c8be9ba0c`.
The initial import preserves the source bytes, licenses and copyright notices.
Libc owns subsequent changes; LLVM Support continues to build its independent
copy. Internal exported symbols are renamed by the libc build, avoiding
collisions with applications that also link LLVM Support.

Initial SHA-256 identities:

| Source | SHA-256 |
| --- | --- |
| regcomp.c | d595a21ef0426caadd619312b8ee443c8f143dd053db4042564f4dd096b90fc8 |
| regexec.c | b26ab983413177c81034ad4dfa66280697fc304a8b27d15b5eb7bf1955cb9745 |
| regfree.c | cab827cecccf9f9446f1b1990b7ad5ae3ba930ba004c3ea04daed5753ce4557d |
| regstrlcpy.c | b717e3355fdca040c161a7a179de9db1c9253da6519e60b77a074cbf5f7d423f |
| regex_impl.h | 90cf33f4d2c50f516b669e8d17310e94f9241eb33b3ca2098478c41dab130f52 |
| regex2.h | ce6f38306170673c9bda3a5d2d638e7af858662d388e0b3884372677998fb7dd |
| regengine.inc | cfbc4a3fbf0d7e7dc806d3f68e4b3f3042cdce6306ffc22e6f62b232ca1efe6a |
| regutils.h | de58605e51879c930ea9ae97541752cf03fc7841d3b6a3d24fd407f203754afc |

The public wrappers select the character composition and retain the two locale
categories until engine destruction. Minimal repetition remains unimplemented.
Source ownership and focused component evidence must not be confused with
complete public capability support.

## Local Changes

The import hashes above identify the original baseline, not the current
derivative. Backreference anchor checks now require a preceding input byte
before reading it; a guard-page test covers both beginning-of-line and the
retained BSD beginning-of-word extension.

The derivative now compiles and executes Unicode scalar sequences. The byte
facade maps unsigned bytes to scalars, preserving existing byte semantics.
Sparse interval sets replace the fixed alphabet bitmap; classification and
case mapping can borrow immutable locale callbacks. A private bracket service
supplies locale predicates and multi-character collating elements, lowering
the latter into the existing BSD alternation strip. Captures, repetitions and
backreferences continue to use that engine. The C++ composition maps scalar
positions back to original input bytes, including stateful shift boundaries.
Internal offsets use ptrdiff_t rather than a filesystem type.

Strip insertion and repetition copying stop after allocation failure instead
of writing beyond the old allocation. Focused failure injection covers this
path and bracket ownership; the byte facade, guard-page and scalar composition
tests remain separate observers. LLVM Support's independent copy is unchanged.

Finite empty backreferences advance through the strip without an arbitrary
reference-count cutoff. Empty repetition cycles still stop at the loop's
progress check. Failed reference validation at the end of the input returns
REG_NOMATCH, not an NFA candidate falsely reported as a successful match.

Extended reference numbers are checked during decimal accumulation, before
integer arithmetic or group-array indexing. This preserves the retained
twenty-group extension limit and valid leading zeros without accepting wrapped
indices. It does not add this extension to the POSIX contract.
