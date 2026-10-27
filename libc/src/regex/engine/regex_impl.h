/*-
 * This code is derived from OpenBSD's libc/regex, original license follows:
 *
 * Copyright (c) 1992 Henry Spencer.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Henry Spencer of the University of Toronto.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)regex.h	8.1 (Berkeley) 6/2/93
 */

#ifndef LLVM_SUPPORT_REGEX_IMPL_H
#define LLVM_SUPPORT_REGEX_IMPL_H

#include <stddef.h>
#include <stdint.h>
typedef int32_t llvm_regex_char_t;
/* Category views are immutable and borrowed for the compiled object's life.
 * Class ids follow the standard names in the order of the enum below. */
enum llvm_regex_class {
  LLVM_REGEX_ALNUM, LLVM_REGEX_ALPHA, LLVM_REGEX_BLANK, LLVM_REGEX_CNTRL,
  LLVM_REGEX_DIGIT, LLVM_REGEX_GRAPH, LLVM_REGEX_LOWER, LLVM_REGEX_PRINT,
  LLVM_REGEX_PUNCT, LLVM_REGEX_SPACE, LLVM_REGEX_UPPER, LLVM_REGEX_XDIGIT
};
typedef struct {
  const void *context;
  int (*classify)(const void *, unsigned, llvm_regex_char_t);
  llvm_regex_char_t (*map)(const void *, llvm_regex_char_t, int upper);
} llvm_regex_ctype;
typedef struct {
  void *context;
  int (*contains)(const void *, llvm_regex_char_t);
  int (*sequences)(const void *, void *,
                   int (*)(void *, const llvm_regex_char_t *, size_t));
  void (*destroy)(void *);
} llvm_regex_bracket;
typedef struct {
  const void *context;
  int (*compile)(const void *, const llvm_regex_char_t *, size_t, size_t *,
                 llvm_regex_bracket *);
} llvm_regex_bracket_compiler;
typedef ptrdiff_t llvm_regoff_t;
typedef struct {
  llvm_regoff_t rm_so;		/* start of match */
  llvm_regoff_t rm_eo;		/* end of match */
} llvm_regmatch_t;

typedef struct llvm_regex {
  int re_magic;
  size_t re_nsub;		/* number of parenthesized subexpressions */
  const char *re_endp;	/* end pointer for REG_PEND */
  struct re_guts *re_g;	/* none of your business :-) */
} llvm_regex_t;

/* llvm_regcomp() flags */
#define	REG_BASIC	0000
#define	REG_EXTENDED	0001
#define	REG_ICASE	0002
#define	REG_NOSUB	0004
#define	REG_NEWLINE	0010
#define	REG_NOSPEC	0020
#define	REG_PEND	0040
#define	REG_DUMP	0200
/* Deliberately distinct from the public REG_MINIMAL bit. */
#define LLVM_REGEX_MINIMAL 0x10000

/* llvm_regerror() flags */
#define	REG_NOMATCH	 1
#define	REG_BADPAT	 2
#define	REG_ECOLLATE	 3
#define	REG_ECTYPE	 4
#define	REG_EESCAPE	 5
#define	REG_ESUBREG	 6
#define	REG_EBRACK	 7
#define	REG_EPAREN	 8
#define	REG_EBRACE	 9
#define	REG_BADBR	10
#define	REG_ERANGE	11
#define	REG_ESPACE	12
#define	REG_BADRPT	13
#define	REG_EMPTY	14
#define	REG_ASSERT	15
#define	REG_INVARG	16
#define	REG_ATOI	255	/* convert name to number (!) */
#define	REG_ITOA	0400	/* convert number to name (!) */

/* llvm_regexec() flags */
#define	REG_NOTBOL	00001
#define	REG_NOTEOL	00002
#define	REG_STARTEND	00004
#define	REG_TRACE	00400	/* tracing of execution */
#define	REG_LARGE	01000	/* force large representation */
#define	REG_BACKR	02000	/* force use of backref code */

#ifdef __cplusplus
extern "C" {
#endif

int	llvm_regcomp(llvm_regex_t *, const char *, int);
size_t	llvm_regerror(int, const llvm_regex_t *, char *, size_t);
int	llvm_regexec(const llvm_regex_t *, const char *, size_t,
                     llvm_regmatch_t [], int);
/* Private scalar interfaces. Match positions count characters, not bytes.
 * Inputs contain len valid Unicode scalars followed by a zero sentinel.
 * The _l compiler borrows category callbacks; the plain compiler retains
 * byte classification and codepoint ranges. */
int __llvm_libc_bsd_regcomp_chars(llvm_regex_t *, const llvm_regex_char_t *,
                                size_t, int);
int __llvm_libc_bsd_regcomp_chars_l(llvm_regex_t *, const llvm_regex_char_t *,
                                  size_t, int, const llvm_regex_ctype *,
                                  const llvm_regex_bracket_compiler *);
int __llvm_libc_bsd_regexec_chars(const llvm_regex_t *,
                                const llvm_regex_char_t *, size_t, size_t,
                                llvm_regmatch_t [], int);
void	llvm_regfree(llvm_regex_t *);
size_t  llvm_strlcpy(char *dst, const char *src, size_t siz);

#ifdef __cplusplus
}
#endif

#endif /* LLVM_SUPPORT_REGEX_IMPL_H */
