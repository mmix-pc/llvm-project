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

The import alone does not add character-aware matching or minimal repetition.
The byte-engine restrictions remain until their implementation and validation
are complete; source ownership must not be confused with capability support.
