/*-
 * This code is derived from OpenBSD's libc/regex, original license follows:
 *
 * Copyright (c) 1992, 1993, 1994 Henry Spencer.
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Henry Spencer.
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
 *	@(#)regcomp.c	8.5 (Berkeley) 3/20/94
 */

#include "regex_impl.h"
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "regex2.h"
#include "regutils.h"

/* character-class table */
static struct cclass {
  const char *name;
  const char *chars;
  const char *multis;
} cclasses[] = {
    {"alnum", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\
0123456789",
     ""},
    {"alpha", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", ""},
    {"blank", " \t", ""},
    {"cntrl", "\007\b\t\n\v\f\r\1\2\3\4\5\6\16\17\20\21\22\23\24\
\25\26\27\30\31\32\33\34\35\36\37\177",
     ""},
    {"digit", "0123456789", ""},
    {"graph", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\
0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
     ""},
    {"lower", "abcdefghijklmnopqrstuvwxyz", ""},
    {"print", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\
0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ ",
     ""},
    {"punct", "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", ""},
    {"space", "\t\n\v\f\r ", ""},
    {"upper", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", ""},
    {"xdigit", "0123456789ABCDEFabcdef", ""},
    {NULL, 0, ""}};

/* character-name table */
static struct cname {
  const char *name;
  char code;
} cnames[] = {{"NUL", '\0'},
              {"SOH", '\001'},
              {"STX", '\002'},
              {"ETX", '\003'},
              {"EOT", '\004'},
              {"ENQ", '\005'},
              {"ACK", '\006'},
              {"BEL", '\007'},
              {"alert", '\007'},
              {"BS", '\010'},
              {"backspace", '\b'},
              {"HT", '\011'},
              {"tab", '\t'},
              {"LF", '\012'},
              {"newline", '\n'},
              {"VT", '\013'},
              {"vertical-tab", '\v'},
              {"FF", '\014'},
              {"form-feed", '\f'},
              {"CR", '\015'},
              {"carriage-return", '\r'},
              {"SO", '\016'},
              {"SI", '\017'},
              {"DLE", '\020'},
              {"DC1", '\021'},
              {"DC2", '\022'},
              {"DC3", '\023'},
              {"DC4", '\024'},
              {"NAK", '\025'},
              {"SYN", '\026'},
              {"ETB", '\027'},
              {"CAN", '\030'},
              {"EM", '\031'},
              {"SUB", '\032'},
              {"ESC", '\033'},
              {"IS4", '\034'},
              {"FS", '\034'},
              {"IS3", '\035'},
              {"GS", '\035'},
              {"IS2", '\036'},
              {"RS", '\036'},
              {"IS1", '\037'},
              {"US", '\037'},
              {"space", ' '},
              {"exclamation-mark", '!'},
              {"quotation-mark", '"'},
              {"number-sign", '#'},
              {"dollar-sign", '$'},
              {"percent-sign", '%'},
              {"ampersand", '&'},
              {"apostrophe", '\''},
              {"left-parenthesis", '('},
              {"right-parenthesis", ')'},
              {"asterisk", '*'},
              {"plus-sign", '+'},
              {"comma", ','},
              {"hyphen", '-'},
              {"hyphen-minus", '-'},
              {"period", '.'},
              {"full-stop", '.'},
              {"slash", '/'},
              {"solidus", '/'},
              {"zero", '0'},
              {"one", '1'},
              {"two", '2'},
              {"three", '3'},
              {"four", '4'},
              {"five", '5'},
              {"six", '6'},
              {"seven", '7'},
              {"eight", '8'},
              {"nine", '9'},
              {"colon", ':'},
              {"semicolon", ';'},
              {"less-than-sign", '<'},
              {"equals-sign", '='},
              {"greater-than-sign", '>'},
              {"question-mark", '?'},
              {"commercial-at", '@'},
              {"left-square-bracket", '['},
              {"backslash", '\\'},
              {"reverse-solidus", '\\'},
              {"right-square-bracket", ']'},
              {"circumflex", '^'},
              {"circumflex-accent", '^'},
              {"underscore", '_'},
              {"low-line", '_'},
              {"grave-accent", '`'},
              {"left-brace", '{'},
              {"left-curly-bracket", '{'},
              {"vertical-line", '|'},
              {"right-brace", '}'},
              {"right-curly-bracket", '}'},
              {"tilde", '~'},
              {"DEL", '\177'},
              {NULL, 0}};

/*
 * parse structure, passed up and down to avoid global variables and
 * other clumsinesses
 */
struct parse {
  const llvm_regex_char_t *begin;
  int tagged;
  size_t tag_capacity;
  const llvm_regex_char_t *next; /* next character in RE */
  const llvm_regex_char_t *end;  /* end of string (-> NUL normally) */
  int error;        /* has an error been seen? */
  sop *strip;       /* malloced strip */
  sopno ssize;      /* malloced strip size (allocated) */
  sopno slen;       /* malloced strip length (used) */
  int ncsalloc;     /* number of csets allocated */
  struct re_guts *g;
  const llvm_regex_bracket_compiler *brackets;
#define NPAREN 21       /* we need to remember () 1-20 for back refs */
  sopno pbegin[NPAREN]; /* -> ( ([0] unused) */
  sopno pend[NPAREN];   /* -> ) ([0] unused) */
};

static void p_ere(struct parse *, int);
static void p_ere_exp(struct parse *);
static void tag_atom(struct parse *, sopno, size_t, int);
static void p_str(struct parse *);
static void p_bre(struct parse *, int, int);
static int p_simp_re(struct parse *, int);
static int p_count(struct parse *);
static void p_bracket(struct parse *);
static void p_locale_bracket(struct parse *);
static void p_b_term(struct parse *, cset *);
static void p_b_cclass(struct parse *, cset *);
static void p_b_eclass(struct parse *, cset *);
static llvm_regex_char_t p_b_symbol(struct parse *);
static llvm_regex_char_t p_b_coll_elem(struct parse *, int);
static int othercase(int);
static void bothcases(struct parse *, int);
static void ordinary(struct parse *, int);
static void nonnewline(struct parse *);
static void repeat(struct parse *, sopno, int, int);
static int seterr(struct parse *, int);
static cset *allocset(struct parse *);
static void freeset(struct parse *, cset *);
static int freezeset(struct parse *, cset *);
static int firstch(struct parse *, cset *);
static int nch(struct parse *, cset *);
static void addrange(struct parse *, cset *, llvm_regex_char_t,
                     llvm_regex_char_t);
static void addliteral(struct parse *, cset *, llvm_regex_char_t);
#define CHadd(cs, c) addliteral(p, cs, c)
static sopno dupl(struct parse *, sopno, sopno);
static void doemit(struct parse *, sop, size_t);
static void doinsert(struct parse *, sop, size_t, sopno);
static void dofwd(struct parse *, sopno, sop);
static void enlarge(struct parse *, sopno);
static void stripsnug(struct parse *, struct re_guts *);
static void findmust(struct parse *, struct re_guts *);
static sopno pluscount(struct parse *, struct re_guts *);

static llvm_regex_char_t nuls[10]; /* scanner target after an error */

static int ascii_equal(const llvm_regex_char_t *text, const char *ascii,
                       size_t count) {
  for (size_t i = 0; i < count; ++i)
    if (text[i] != (unsigned char)ascii[i])
      return 0;
  return 1;
}

static int ascii_digit(llvm_regex_char_t c) { return c >= '0' && c <= '9'; }
static int ascii_alpha(llvm_regex_char_t c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/*
 * macros for use with parse structure
 * BEWARE:  these know that the parse structure is named `p' !!!
 */
#define PEEK() (*p->next)
#define PEEK2() (*(p->next + 1))
#define MORE() (p->end - p->next > 0)
#define MORE2() (p->end - p->next > 1)
#define SEE(c) (MORE() && PEEK() == (c))
#define SEETWO(a, b) (MORE2() && PEEK() == (a) && PEEK2() == (b))
#define EAT(c) ((SEE(c)) ? (NEXT(), 1) : 0)
#define EATTWO(a, b) ((SEETWO(a, b)) ? (NEXT2(), 1) : 0)
#define NEXT() (p->next++)
#define NEXT2() (p->next += 2)
#define NEXTn(n) (p->next += (n))
#define GETNEXT() (*p->next++)
#define SETERROR(e) seterr(p, (e))
#define REQUIRE(co, e) (void)((co) || SETERROR(e))
#define MUSTSEE(c, e) (REQUIRE(MORE() && PEEK() == (c), e))
#define MUSTEAT(c, e) (REQUIRE(MORE() && GETNEXT() == (c), e))
#define MUSTNOTSEE(c, e) (REQUIRE(!MORE() || PEEK() != (c), e))
#define EMIT(op, sopnd) doemit(p, (sop)(op), (size_t)(sopnd))
#define INSERT(op, pos) doinsert(p, (sop)(op), HERE() - (pos) + 1, pos)
#define AHEAD(pos) dofwd(p, pos, HERE() - (pos))
#define ASTERN(sop, pos) EMIT(sop, HERE() - pos)
#define HERE() (p->slen)
#define THERE() (p->slen - 1)
#define THERETHERE() (p->slen - 2)
#define DROP(n) (p->slen -= (n))

#ifdef _POSIX2_RE_DUP_MAX
#define DUPMAX _POSIX2_RE_DUP_MAX
#else
#define DUPMAX 255
#endif
#define REGINFINITY (DUPMAX + 1)

#ifndef NDEBUG
static int never = 0; /* for use in asserts; shuts lint up */
#else
#define never 0 /* some <assert.h>s have bugs too */
#endif

/*
 - llvm_regcomp - interface for parser and compilation
 */
int /* 0 success, otherwise REG_something */
llvm_regcomp(llvm_regex_t *preg, const char *pattern, int cflags) {
  size_t len;
  if (cflags & REG_PEND) {
    if (preg->re_endp < pattern)
      return REG_INVARG;
    len = preg->re_endp - pattern;
  } else {
    len = strlen(pattern);
  }
  if (len >= SIZE_MAX / sizeof(llvm_regex_char_t))
    return REG_ESPACE;
  llvm_regex_char_t *chars = malloc((len + 1) * sizeof(*chars));
  if (!chars)
    return REG_ESPACE;
  for (size_t i = 0; i < len; ++i)
    chars[i] = (unsigned char)pattern[i];
  chars[len] = 0;
  int result = __llvm_libc_bsd_regcomp_chars(preg, chars, len,
                                           cflags & ~REG_PEND);
  free(chars);
  return result;
}

int __llvm_libc_bsd_regcomp_chars(llvm_regex_t *preg,
                                 const llvm_regex_char_t *pattern, size_t len,
                                 int cflags) {
  return __llvm_libc_bsd_regcomp_chars_l(preg, pattern, len, cflags, NULL, NULL);
}

int __llvm_libc_bsd_regcomp_chars_l(llvm_regex_t *preg,
                                   const llvm_regex_char_t *pattern, size_t len,
                                   int cflags, const llvm_regex_ctype *ctype,
                                   const llvm_regex_bracket_compiler *brackets) {
  struct parse pa;
  struct re_guts *g;
  struct parse *p = &pa;
  int i;
#ifdef REDEBUG
#define GOODFLAGS(f) (f)
#else
#define GOODFLAGS(f) ((f) & ~REG_DUMP)
#endif

  cflags = GOODFLAGS(cflags);
  if ((cflags & REG_EXTENDED) && (cflags & REG_NOSPEC))
    return (REG_INVARG);

  if (cflags & REG_PEND)
    return REG_INVARG;
  if (ctype && (!ctype->map || !ctype->classify))
    return REG_INVARG;
  if (brackets && !brackets->compile)
    return REG_INVARG;
  if (len > (size_t)LONG_MAX / 3 || len >= SIZE_MAX / sizeof(sop) / 3)
    return REG_ESPACE;
  for (size_t at = 0; at < len; ++at)
    if (pattern[at] < 0 || pattern[at] >= OUT ||
        (pattern[at] >= 0xd800 && pattern[at] <= 0xdfff))
      return REG_BADPAT;

  /* do the mallocs early so failure handling is easy */
  g = (struct re_guts *)malloc(sizeof(struct re_guts));
  if (g == NULL)
    return (REG_ESPACE);
  p->ssize = len / (size_t)2 * (size_t)3 + (size_t)1; /* ugh */
  p->strip = (sop *)calloc(p->ssize, sizeof(sop));
  p->slen = 0;
  if (p->strip == NULL) {
    free((char *)g);
    return (REG_ESPACE);
  }

  /* set things up */
  p->g = g;
  p->brackets = brackets;
  p->next = pattern;
  p->begin = pattern;
  p->tag_capacity = 0;
  p->tagged = (cflags & REG_EXTENDED) && (cflags & LLVM_REGEX_MINIMAL);
  if (cflags & REG_EXTENDED)
    for (size_t at = 0; at < len; ++at)
      p->tagged |= pattern[at] == '?';
  p->end = p->next + len;
  p->error = 0;
  p->ncsalloc = 0;
  for (i = 0; i < NPAREN; i++) {
    p->pbegin[i] = 0;
    p->pend[i] = 0;
  }
  g->sets = NULL;
  g->tags = NULL;
  g->ntags = 0;
  g->minimal = 0;
  g->ncsets = 0;
  g->cflags = cflags;
  g->ctype = ctype ? *ctype : (llvm_regex_ctype){0};
  g->iflags = 0;
  g->nbol = 0;
  g->neol = 0;
  g->must = NULL;
  g->mlen = 0;
  g->nsub = 0;
  g->backrefs = 0;

  /* do it */
  EMIT(OEND, 0);
  g->firststate = THERE();
  if (cflags & REG_EXTENDED)
    p_ere(p, OUT);
  else if (cflags & REG_NOSPEC)
    p_str(p);
  else
    p_bre(p, OUT, OUT);
  EMIT(OEND, 0);
  g->laststate = THERE();

  /* tidy up loose ends and fill things in */
  stripsnug(p, g);
  findmust(p, g);
  g->nplus = pluscount(p, g);
  g->magic = MAGIC2;
  preg->re_nsub = g->nsub;
  preg->re_g = g;
  preg->re_magic = MAGIC1;
#ifndef REDEBUG
  /* not debugging, so can't rely on the assert() in llvm_regexec() */
  if (g->iflags & REGEX_BAD)
    SETERROR(REG_ASSERT);
#endif

  /* win or lose, we're done */
  if (p->error != 0) /* lose */
    llvm_regfree(preg);
  return (p->error);
}

/*
 - p_ere - ERE parser top level, concatenation and alternation
 */
static void p_ere(struct parse *p,
                  int stop) /* character this ERE should end at */
{
  llvm_regex_char_t c;
  sopno prevback = 0;
  sopno prevfwd = 0;
  sopno conc;
  int first = 1; /* is this the first alternative? */

  for (;;) {
    /* do a bunch of concatenated expressions */
    conc = HERE();
    while (MORE() && (c = PEEK()) != '|' && c != stop)
      p_ere_exp(p);
    REQUIRE(HERE() != conc, REG_EMPTY); /* require nonempty */

    if (!EAT('|'))
      break; /* NOTE BREAK OUT */

    if (first) {
      INSERT(OCH_, conc); /* offset is wrong */
      prevfwd = conc;
      prevback = conc;
      first = 0;
    }
    ASTERN(OOR1, prevback);
    prevback = THERE();
    AHEAD(prevfwd); /* fix previous offset */
    prevfwd = HERE();
    EMIT(OOR2, 0); /* offset is very wrong */
  }

  if (!first) { /* tail-end fixups */
    AHEAD(prevfwd);
    ASTERN(O_CH, prevback);
  }

  assert(!MORE() || SEE(stop));
}

/*
 - p_ere_exp - parse one subERE, an atom possibly followed by a repetition op
 */
static void p_ere_exp(struct parse *p) {
  llvm_regex_char_t c;
  sopno pos;
  int count;
  int count2;
  int backrefnum;
  sopno subno;
  int wascaret = 0;
  size_t source = p->next - p->begin;

  assert(MORE()); /* caller should have ensured this */
  c = GETNEXT();

  pos = HERE();
  switch (c) {
  case '(':
    REQUIRE(MORE(), REG_EPAREN);
    p->g->nsub++;
    subno = p->g->nsub;
    if (subno < NPAREN)
      p->pbegin[subno] = HERE();
    EMIT(OLPAREN, subno);
    if (!SEE(')'))
      p_ere(p, ')');
    if (subno < NPAREN) {
      p->pend[subno] = HERE();
      assert(p->pend[subno] != 0);
    }
    EMIT(ORPAREN, subno);
    MUSTEAT(')', REG_EPAREN);
    break;
#ifndef POSIX_MISTAKE
  case ')': /* happens only if no current unmatched ( */
    /*
     * You may ask, why the ifndef?  Because I didn't notice
     * this until slightly too late for 1003.2, and none of the
     * other 1003.2 regular-expression reviewers noticed it at
     * all.  So an unmatched ) is legal POSIX, at least until
     * we can get it fixed.
     */
    SETERROR(REG_EPAREN);
    break;
#endif
  case '^':
    EMIT(OBOL, 0);
    p->g->iflags |= USEBOL;
    p->g->nbol++;
    wascaret = 1;
    break;
  case '$':
    EMIT(OEOL, 0);
    p->g->iflags |= USEEOL;
    p->g->neol++;
    break;
  case '|':
    SETERROR(REG_EMPTY);
    break;
  case '*':
  case '+':
  case '?':
    SETERROR(REG_BADRPT);
    break;
  case '.':
    if (p->g->cflags & REG_NEWLINE)
      nonnewline(p);
    else
      EMIT(OANY, 0);
    break;
  case '[':
    p_bracket(p);
    break;
  case '\\':
    REQUIRE(MORE(), REG_EESCAPE);
    c = GETNEXT();
    if (c >= '1' && c <= '9') {
      /* \[0-9] is taken to be a back-reference to a previously specified
       * matching group. backrefnum will hold the number. The matching
       * group must exist (i.e. if \4 is found there must have been at
       * least 4 matching groups specified in the pattern previously).
       */
      backrefnum = c - '0';
    } else if (c == 'g') {
      /* Support back-references with index greater 9.
       * These look like that: \g{n}
       * Extract the number inside the brackets.
       */
      MUSTEAT('{', REG_BADRPT);

      backrefnum = 0;
      while (MORE() && ascii_digit(PEEK())) {
        c = GETNEXT();
        int digit = c - '0';
        // Bound the index before arithmetic and before accessing paren arrays.
        if (backrefnum > (NPAREN - 1 - digit) / 10) {
          SETERROR(REG_ESUBREG);
          return;
        }
        backrefnum = backrefnum * 10 + digit;
      }
      MUSTEAT('}', REG_BADRPT);
    } else {
      /* Other chars are simply themselves when escaped with a backslash.
       */
      ordinary(p, c);
      break;
    }

    if (backrefnum == 0 || backrefnum >= NPAREN) {
      SETERROR(REG_ESUBREG);
      break;
    }

    if (p->pend[backrefnum] == 0) {
      SETERROR(REG_ESUBREG);
      break;
    }

    /* Make sure everything checks out and emit the sequence
     * that marks a back-reference to the parse structure.
     */
    assert(backrefnum <= p->g->nsub);
    EMIT(OBACK_, backrefnum);
    assert(p->pbegin[backrefnum] != 0);
    assert(OP(p->strip[p->pbegin[backrefnum]]) == OLPAREN);
    assert(OP(p->strip[p->pend[backrefnum]]) == ORPAREN);
    (void)dupl(p, p->pbegin[backrefnum] + 1, p->pend[backrefnum]);
    EMIT(O_BACK, backrefnum);
    p->g->backrefs = 1;
    break;
  case '{': /* okay as ordinary except if digit follows */
    REQUIRE(!MORE() || !ascii_digit(PEEK()), REG_BADRPT);
    REGEX_FALLTHROUGH;
  default:
    ordinary(p, c);
    break;
  }

  if (!MORE()) {
    tag_atom(p, pos, source * 2 + 1, 0);
    return;
  }
  c = PEEK();
  /* we call { a repetition if followed by a digit */
  if (!(c == '*' || c == '+' || c == '?' ||
        (c == '{' && MORE2() && ascii_digit(PEEK2())))) {
    tag_atom(p, pos, source * 2 + 1, 0);
    return; /* no repetition, we're done */
  }
  // Preserve each atom visit as well as the complete repetition interval.
  tag_atom(p, pos, source * 2 + 1, 0);
  NEXT();

  REQUIRE(!wascaret, REG_BADRPT);
  switch (c) {
  case '*': /* implemented as +? */
    /* this case does not require the (y|) trick, noKLUDGE */
    INSERT(OPLUS_, pos);
    ASTERN(O_PLUS, pos);
    INSERT(OQUEST_, pos);
    ASTERN(O_QUEST, pos);
    break;
  case '+':
    INSERT(OPLUS_, pos);
    ASTERN(O_PLUS, pos);
    break;
  case '?':
    /* KLUDGE: emit y? as (y|) until subtle bug gets fixed */
    INSERT(OCH_, pos); /* offset slightly wrong */
    ASTERN(OOR1, pos); /* this one's right */
    AHEAD(pos);        /* fix the OCH_ */
    EMIT(OOR2, 0);     /* offset very wrong... */
    AHEAD(THERE());    /* ...so fix it */
    ASTERN(O_CH, THERETHERE());
    break;
  case '{':
    count = p_count(p);
    if (EAT(',')) {
      if (ascii_digit(PEEK())) {
        count2 = p_count(p);
        REQUIRE(count <= count2, REG_BADBR);
      } else /* single number with comma */
        count2 = REGINFINITY;
    } else /* just a single number */
      count2 = count;
    repeat(p, pos, count, count2);
    if (!EAT('}')) { /* error heuristics */
      while (MORE() && PEEK() != '}')
        NEXT();
      REQUIRE(MORE(), REG_EBRACE);
      SETERROR(REG_BADBR);
    }
    break;
  }

  int minimal = !!(p->g->cflags & LLVM_REGEX_MINIMAL);
  if (EAT('?'))
    minimal = !minimal;
  tag_atom(p, pos, source * 2, minimal);
  if (!MORE())
    return;
  c = PEEK();
  if (!(c == '*' || c == '+' || c == '?' ||
        (c == '{' && MORE2() && ascii_digit(PEEK2()))))
    return;
  SETERROR(REG_BADRPT);
}

/* Tags survive strip duplication: each visit records a separate occurrence,
 * while source order remains independent of the parser's postorder. */
static void tag_atom(struct parse *p, sopno start, size_t source, int minimal) {
  if (!p->tagged || p->error)
    return;
  size_t id = p->g->ntags;
  if (id >= OPDMASK || id >= SIZE_MAX / sizeof(regex_tag) - 1) {
    SETERROR(REG_ESPACE);
    return;
  }
  if (id == p->tag_capacity) {
    size_t capacity = p->tag_capacity * 2 + 8;
    if (capacity > SIZE_MAX / sizeof(regex_tag))
      capacity = id + 1;
    regex_tag *tags = realloc(p->g->tags, capacity * sizeof(*tags));
    if (!tags) {
      SETERROR(REG_ESPACE);
      return;
    }
    p->g->tags = tags;
    p->tag_capacity = capacity;
  }
  p->g->tags[id] = (regex_tag){source, minimal};
  ++p->g->ntags;
  p->g->minimal |= minimal;
  doinsert(p, OTAG_, id, start);
  EMIT(O_TAG, id);
}

/*
 - p_str - string (no metacharacters) "parser"
 */
static void p_str(struct parse *p) {
  REQUIRE(MORE(), REG_EMPTY);
  while (MORE())
    ordinary(p, GETNEXT());
}

/*
 - p_bre - BRE parser top level, anchoring and concatenation
 * Giving end1 as OUT essentially eliminates the end1/end2 check.
 *
 * This implementation is a bit of a kludge, in that a trailing $ is first
 * taken as an ordinary character and then revised to be an anchor.  The
 * only undesirable side effect is that '$' gets included as a character
 * category in such cases.  This is fairly harmless; not worth fixing.
 * The amount of lookahead needed to avoid this kludge is excessive.
 */
static void p_bre(struct parse *p, int end1, /* first terminating character */
                  int end2)                  /* second terminating character */
{
  sopno start = HERE();
  int first = 1; /* first subexpression? */
  int wasdollar = 0;

  if (EAT('^')) {
    EMIT(OBOL, 0);
    p->g->iflags |= USEBOL;
    p->g->nbol++;
  }
  while (MORE() && !SEETWO(end1, end2)) {
    wasdollar = p_simp_re(p, first);
    first = 0;
  }
  if (wasdollar) { /* oops, that was a trailing anchor */
    DROP(1);
    EMIT(OEOL, 0);
    p->g->iflags |= USEEOL;
    p->g->neol++;
  }

  REQUIRE(HERE() != start, REG_EMPTY); /* require nonempty */
}

/*
 - p_simp_re - parse a simple RE, an atom possibly followed by a repetition
 */
static int /* was the simple RE an unbackslashed $? */
p_simp_re(struct parse *p,
          int starordinary) /* is a leading * an ordinary character? */
{
  int c;
  int count;
  int count2;
  sopno pos;
  int i;
  sopno subno;
#define BACKSL (1 << 21)

  pos = HERE(); /* repetition op, if any, covers from here */

  assert(MORE()); /* caller should have ensured this */
  c = GETNEXT();
  if (c == '\\') {
    REQUIRE(MORE(), REG_EESCAPE);
    c = BACKSL | GETNEXT();
  }
  switch (c) {
  case '.':
    if (p->g->cflags & REG_NEWLINE)
      nonnewline(p);
    else
      EMIT(OANY, 0);
    break;
  case '[':
    p_bracket(p);
    break;
  case BACKSL | '{':
    SETERROR(REG_BADRPT);
    break;
  case BACKSL | '(':
    p->g->nsub++;
    subno = p->g->nsub;
    if (subno < NPAREN)
      p->pbegin[subno] = HERE();
    EMIT(OLPAREN, subno);
    /* the MORE here is an error heuristic */
    if (MORE() && !SEETWO('\\', ')'))
      p_bre(p, '\\', ')');
    if (subno < NPAREN) {
      p->pend[subno] = HERE();
      assert(p->pend[subno] != 0);
    }
    EMIT(ORPAREN, subno);
    REQUIRE(EATTWO('\\', ')'), REG_EPAREN);
    break;
  case BACKSL | ')': /* should not get here -- must be user */
  case BACKSL | '}':
    SETERROR(REG_EPAREN);
    break;
  case BACKSL | '1':
  case BACKSL | '2':
  case BACKSL | '3':
  case BACKSL | '4':
  case BACKSL | '5':
  case BACKSL | '6':
  case BACKSL | '7':
  case BACKSL | '8':
  case BACKSL | '9':
    i = (c & ~BACKSL) - '0';
    assert(i < NPAREN);
    if (p->pend[i] != 0) {
      assert(i <= p->g->nsub);
      EMIT(OBACK_, i);
      assert(p->pbegin[i] != 0);
      assert(OP(p->strip[p->pbegin[i]]) == OLPAREN);
      assert(OP(p->strip[p->pend[i]]) == ORPAREN);
      (void)dupl(p, p->pbegin[i] + 1, p->pend[i]);
      EMIT(O_BACK, i);
    } else {
      SETERROR(REG_ESUBREG);
    }
    p->g->backrefs = 1;
    break;
  case '*':
    REQUIRE(starordinary, REG_BADRPT);
    REGEX_FALLTHROUGH;
  default:
    ordinary(p, c & ~BACKSL);
    break;
  }

  if (EAT('*')) { /* implemented as +? */
    /* this case does not require the (y|) trick, noKLUDGE */
    INSERT(OPLUS_, pos);
    ASTERN(O_PLUS, pos);
    INSERT(OQUEST_, pos);
    ASTERN(O_QUEST, pos);
  } else if (EATTWO('\\', '{')) {
    count = p_count(p);
    if (EAT(',')) {
      if (MORE() && ascii_digit(PEEK())) {
        count2 = p_count(p);
        REQUIRE(count <= count2, REG_BADBR);
      } else /* single number with comma */
        count2 = REGINFINITY;
    } else /* just a single number */
      count2 = count;
    repeat(p, pos, count, count2);
    if (!EATTWO('\\', '}')) { /* error heuristics */
      while (MORE() && !SEETWO('\\', '}'))
        NEXT();
      REQUIRE(MORE(), REG_EBRACE);
      SETERROR(REG_BADBR);
    }
  } else if (c == '$') /* $ (but not \$) ends it */
    return (1);

  return (0);
}

/*
 - p_count - parse a repetition count
 */
static int /* the value */
p_count(struct parse *p) {
  int count = 0;
  int ndigits = 0;

  while (MORE() && ascii_digit(PEEK()) && count <= DUPMAX) {
    count = count * 10 + (GETNEXT() - '0');
    ndigits++;
  }

  REQUIRE(ndigits > 0 && count <= DUPMAX, REG_BADBR);
  return (count);
}

/*
 - p_bracket - parse a bracketed character list
 *
 * Note a significant property of this code:  if the allocset() did SETERROR,
 * no set operations are done.
 */
static void p_bracket(struct parse *p) {
  cset *cs;
  int invert = 0;

  /* Dept of Truly Sickening Special-Case Kludges */
  if (p->end - p->next > 5) {
    if (ascii_equal(p->next, "[:<:]]", 6)) {
      EMIT(OBOW, 0);
      NEXTn(6);
      return;
    }
    if (ascii_equal(p->next, "[:>:]]", 6)) {
      EMIT(OEOW, 0);
      NEXTn(6);
      return;
    }
  }

  if (p->brackets) {
    p_locale_bracket(p);
    return;
  }

  if ((cs = allocset(p)) == NULL) {
    /* allocset did set error status in p */
    return;
  }

  if (EAT('^'))
    invert++; /* make note to invert set at end */
  if (EAT(']'))
    CHadd(cs, ']');
  else if (EAT('-'))
    CHadd(cs, '-');
  while (MORE() && PEEK() != ']' && !SEETWO('-', ']'))
    p_b_term(p, cs);
  if (EAT('-'))
    CHadd(cs, '-');
  MUSTEAT(']', REG_EBRACK);

  if (p->error != 0) { /* don't mess things up further */
    freeset(p, cs);
    return;
  }

  if ((p->g->cflags & REG_ICASE) && !p->g->ctype.map) {
    int i;
    int ci;

    // Locale-aware folding remains separate. Never silently narrow a scalar.
    for (size_t at = 0; at < cs->count; ++at)
      if (cs->ranges[at].last >= NC) {
        SETERROR(REG_ECTYPE);
        return;
      }
    for (i = NC - 1; i >= 0; i--)
      if (CHIN(cs, i) && isalpha(i)) {
        ci = othercase(i);
        if (ci != i)
          CHadd(cs, ci);
      }
  }
  if (invert) {
    cs->inverted = 1;
    cs->exclude_newline = (p->g->cflags & REG_NEWLINE) != 0;
  }

  if (nch(p, cs) == 1) { /* optimize singleton sets */
    ordinary(p, firstch(p, cs));
    freeset(p, cs);
  } else {
    EMIT(OANYOF, freezeset(p, cs));
  }
}

/* Collating sequences are alternatives in the same strip as the scalar
 * predicate. This keeps captures, repetition and backtracking in the BSD VM. */
struct bracket_alternatives {
  struct parse *p;
  sopno start, back, forward;
  int first;
};

static int emit_bracket_sequence(void *context, const llvm_regex_char_t *text,
                                 size_t length) {
  struct bracket_alternatives *state = context;
  struct parse *p = state->p;
  if (p->error)
    return p->error;
  if (length < 2)
    return SETERROR(REG_ECOLLATE);
  if (state->first) {
    INSERT(OCH_, state->start);
    state->forward = state->back = state->start;
    state->first = 0;
  }
  ASTERN(OOR1, state->back);
  state->back = THERE();
  AHEAD(state->forward);
  state->forward = HERE();
  EMIT(OOR2, 0);
  for (size_t i = 0; i < length && !p->error; ++i)
    ordinary(p, text[i]);
  return p->error;
}

static void p_locale_bracket(struct parse *p) {
  cset *set = allocset(p);
  if (!set)
    return;
  size_t consumed = 0;
  int error = p->brackets->compile(p->brackets->context, p->next,
                                   (size_t)(p->end - p->next), &consumed,
                                   &set->custom);
  if (error) {
    SETERROR(error);
    return;
  }
  if (!consumed || consumed > (size_t)(p->end - p->next)) {
    SETERROR(REG_EBRACK);
    return;
  }
  NEXTn(consumed);
  // Emitting case-folded sequences can grow/reallocate the set array.
  llvm_regex_bracket custom = set->custom;
  struct bracket_alternatives state = {p, HERE(), 0, 0, 1};
  EMIT(OANYOF, freezeset(p, set));
  error = custom.sequences(custom.context, &state, emit_bracket_sequence);
  if (error) {
    SETERROR(error);
    return;
  }
  if (!state.first) {
    AHEAD(state.forward);
    ASTERN(O_CH, state.back);
  }
}

/*
 - p_b_term - parse one term of a bracketed character list
 */
static void p_b_term(struct parse *p, cset *cs) {
  llvm_regex_char_t c;
  llvm_regex_char_t start, finish;

  /* classify what we've got */
  switch ((MORE()) ? PEEK() : '\0') {
  case '[':
    c = (MORE2()) ? PEEK2() : '\0';
    break;
  case '-':
    SETERROR(REG_ERANGE);
    return; /* NOTE RETURN */
    break;
  default:
    c = '\0';
    break;
  }

  switch (c) {
  case ':': /* character class */
    NEXT2();
    REQUIRE(MORE(), REG_EBRACK);
    c = PEEK();
    REQUIRE(c != '-' && c != ']', REG_ECTYPE);
    p_b_cclass(p, cs);
    REQUIRE(MORE(), REG_EBRACK);
    REQUIRE(EATTWO(':', ']'), REG_ECTYPE);
    break;
  case '=': /* equivalence class */
    NEXT2();
    REQUIRE(MORE(), REG_EBRACK);
    c = PEEK();
    REQUIRE(c != '-' && c != ']', REG_ECOLLATE);
    p_b_eclass(p, cs);
    REQUIRE(MORE(), REG_EBRACK);
    REQUIRE(EATTWO('=', ']'), REG_ECOLLATE);
    break;
  default: /* symbol, ordinary character, or range */
           /* xxx revision needed for multichar stuff */
    start = p_b_symbol(p);
    if (SEE('-') && MORE2() && PEEK2() != ']') {
      /* range */
      NEXT();
      if (EAT('-'))
        finish = '-';
      else
        finish = p_b_symbol(p);
    } else {
      finish = start;
    }
    /* xxx what about signed chars here... */
    REQUIRE(start <= finish, REG_ERANGE);
    if (p->error)
      return;
    if (start == finish)
      CHadd(cs, start);
    else
      addrange(p, cs, start, finish);
    break;
  }
}

/*
 - p_b_cclass - parse a character-class name and deal with it
 */
static void p_b_cclass(struct parse *p, cset *cs) {
  const llvm_regex_char_t *sp = p->next;
  struct cclass *cp;
  size_t len;
  const char *u;
  llvm_regex_char_t c;

  while (MORE() && ascii_alpha(PEEK()))
    NEXT();
  len = p->next - sp;
  for (cp = cclasses; cp->name != NULL; cp++)
    if (strlen(cp->name) == len && ascii_equal(sp, cp->name, len))
      break;
  if (cp->name == NULL) {
    /* oops, didn't find it */
    SETERROR(REG_ECTYPE);
    return;
  }

  if (p->g->ctype.classify) {
    unsigned index = (unsigned)(cp - cclasses);
    cs->classes |= 1U << index;
    if ((p->g->cflags & REG_ICASE) &&
        (index == LLVM_REGEX_LOWER || index == LLVM_REGEX_UPPER))
      cs->classes |= (1U << LLVM_REGEX_LOWER) | (1U << LLVM_REGEX_UPPER);
    return;
  }

  u = cp->chars;
  while ((c = *u++) != '\0')
    CHadd(cs, c);
}

/*
 - p_b_eclass - parse an equivalence-class name and deal with it
 *
 * This implementation is incomplete. xxx
 */
static void p_b_eclass(struct parse *p, cset *cs) {
  llvm_regex_char_t c;

  c = p_b_coll_elem(p, '=');
  if (p->error)
    return;
  CHadd(cs, c);
}

/*
 - p_b_symbol - parse a character or [..]ed multicharacter collating symbol
 */
static llvm_regex_char_t /* value of symbol */
p_b_symbol(struct parse *p) {
  llvm_regex_char_t value;

  REQUIRE(MORE(), REG_EBRACK);
  if (!EATTWO('[', '.'))
    return (GETNEXT());

  /* collating symbol */
  value = p_b_coll_elem(p, '.');
  REQUIRE(EATTWO('.', ']'), REG_ECOLLATE);
  return (value);
}

/*
 - p_b_coll_elem - parse a collating-element name and look it up
 */
static llvm_regex_char_t                 /* value of collating element */
p_b_coll_elem(struct parse *p, int endc) /* name ended by endc,']' */
{
  const llvm_regex_char_t *sp = p->next;
  struct cname *cp;
  size_t len;

  while (MORE() && !SEETWO(endc, ']'))
    NEXT();
  if (!MORE()) {
    SETERROR(REG_EBRACK);
    return (0);
  }
  len = p->next - sp;
  for (cp = cnames; cp->name != NULL; cp++)
    if (strlen(cp->name) == len && ascii_equal(sp, cp->name, len))
      return (cp->code); /* known name */
  if (len == 1)
    return (*sp);         /* single character */
  SETERROR(REG_ECOLLATE); /* neither */
  return (0);
}

/*
 - othercase - return the case counterpart of an alphabetic
 */
static int /* if no counterpart, return ch */
othercase(int ch) {
  ch = (uch)ch;
  assert(isalpha(ch));
  if (isupper(ch))
    return ((uch)tolower(ch));
  else if (islower(ch))
    return ((uch)toupper(ch));
  else /* peculiar, but could happen */
    return (ch);
}

/*
 - bothcases - emit a dualcase version of a two-case character
 *
 * Boy, is this implementation ever a kludge...
 */
static void bothcases(struct parse *p, int ch) {
  const llvm_regex_char_t *oldnext = p->next;
  const llvm_regex_char_t *oldend = p->end;
  llvm_regex_char_t bracket[3];

  ch = (uch)ch;
  assert(othercase(ch) != ch); /* p_bracket() would recurse */
  p->next = bracket;
  p->end = bracket + 2;
  bracket[0] = ch;
  bracket[1] = ']';
  bracket[2] = '\0';
  p_bracket(p);
  assert(p->next == bracket + 2);
  p->next = oldnext;
  p->end = oldend;
}

/*
 - ordinary - emit an ordinary character
 */
static void ordinary(struct parse *p, int ch) {
  if ((p->g->cflags & REG_ICASE) && p->g->ctype.map) {
    cset *set = allocset(p);
    if (!set)
      return;
    CHadd(set, ch);
    EMIT(OANYOF, freezeset(p, set));
    return;
  }
  if ((p->g->cflags & REG_ICASE) && ch >= NC) {
    SETERROR(REG_ECTYPE);
    return;
  }
  if ((p->g->cflags & REG_ICASE) && isalpha(ch) && othercase(ch) != ch)
    bothcases(p, ch);
  else {
    EMIT(OCHAR, ch);
  }
}

/*
 - nonnewline - emit REG_NEWLINE version of OANY
 *
 * Boy, is this implementation ever a kludge...
 */
static void nonnewline(struct parse *p) {
  const llvm_regex_char_t *oldnext = p->next;
  const llvm_regex_char_t *oldend = p->end;
  static const llvm_regex_char_t bracket[4] = {'^', '\n', ']', '\0'};

  p->next = bracket;
  p->end = bracket + 3;
  p_bracket(p);
  assert(p->next == bracket + 3);
  p->next = oldnext;
  p->end = oldend;
}

/*
 - repeat - generate code for a bounded repetition, recursively if needed
 */
static void repeat(struct parse *p,
                   sopno start, /* operand from here to end of strip */
                   int from,    /* repeated from this number */
                   int to)      /* to this number of times (maybe INFINITY) */
{
  sopno finish = HERE();
#define N 2
#define INF 3
#define REP(f, t) ((f) * 8 + (t))
#define MAP(n) (((n) <= 1) ? (n) : ((n) == REGINFINITY) ? INF : N)
  sopno copy;

  if (p->error != 0) /* head off possible runaway recursion */
    return;

  assert(from <= to);

  switch (REP(MAP(from), MAP(to))) {
  case REP(0, 0):         /* must be user doing this */
    DROP(finish - start); /* drop the operand */
    break;
  case REP(0, 1):   /* as x{1,1}? */
  case REP(0, N):   /* as x{1,n}? */
  case REP(0, INF): /* as x{1,}? */
    /* KLUDGE: emit y? as (y|) until subtle bug gets fixed */
    INSERT(OCH_, start); /* offset is wrong... */
    repeat(p, start + 1, 1, to);
    ASTERN(OOR1, start);
    AHEAD(start); /* ... fix it */
    EMIT(OOR2, 0);
    AHEAD(THERE());
    ASTERN(O_CH, THERETHERE());
    break;
  case REP(1, 1): /* trivial case */
    /* done */
    break;
  case REP(1, N): /* as x?x{1,n-1} */
    /* KLUDGE: emit y? as (y|) until subtle bug gets fixed */
    INSERT(OCH_, start);
    ASTERN(OOR1, start);
    AHEAD(start);
    EMIT(OOR2, 0);  /* offset very wrong... */
    AHEAD(THERE()); /* ...so fix it */
    ASTERN(O_CH, THERETHERE());
    copy = dupl(p, start + 1, finish + 1);
    if (p->error)
      return;
    assert(copy == finish + 4);
    repeat(p, copy, 1, to - 1);
    break;
  case REP(1, INF): /* as x+ */
    INSERT(OPLUS_, start);
    ASTERN(O_PLUS, start);
    break;
  case REP(N, N): /* as xx{m-1,n-1} */
    copy = dupl(p, start, finish);
    repeat(p, copy, from - 1, to - 1);
    break;
  case REP(N, INF): /* as xx{n-1,INF} */
    copy = dupl(p, start, finish);
    repeat(p, copy, from - 1, to);
    break;
  default:                /* "can't happen" */
    SETERROR(REG_ASSERT); /* just in case */
    break;
  }
}

/*
 - seterr - set an error condition
 */
static int /* useless but makes type checking happy */
seterr(struct parse *p, int e) {
  if (p->error == 0) /* keep earliest error condition */
    p->error = e;
  p->next = nuls; /* try to bring things to a halt */
  p->end = nuls;
  return (0); /* make the return value well-defined */
}

/*
 - allocset - allocate a set of characters for []
 */
static cset *allocset(struct parse *p) {
  if (p->g->ncsets == p->ncsalloc) {
    if (p->ncsalloc > INT_MAX - 8) {
      SETERROR(REG_ESPACE);
      return NULL;
    }
    size_t count = (size_t)p->ncsalloc + 8;
    if (count > SIZE_MAX / sizeof(cset)) {
      SETERROR(REG_ESPACE);
      return NULL;
    }
    cset *sets = realloc(p->g->sets, count * sizeof(cset));
    if (!sets) {
      SETERROR(REG_ESPACE);
      return NULL;
    }
    p->g->sets = sets;
    p->ncsalloc = (int)count;
  }
  cset *set = &p->g->sets[p->g->ncsets++];
  memset(set, 0, sizeof(*set));
  set->ctype = p->g->ctype;
  set->fold = (p->g->cflags & REG_ICASE) && p->g->ctype.map;
  return set;
}

static void addliteral(struct parse *p, cset *set, llvm_regex_char_t c) {
  addrange(p, set, c, c);
  if (set->fold) {
    llvm_regex_char_t lower = set->ctype.map(set->ctype.context, c, 0);
    llvm_regex_char_t upper = set->ctype.map(set->ctype.context, c, 1);
    addrange(p, set, lower, lower);
    addrange(p, set, upper, upper);
  }
}

/* Append intervals, merging overlaps and neighbours. Inversion is a property
 * of the complete set and never enumerates the character repertoire. */
static void addrange(struct parse *p, cset *set, llvm_regex_char_t first,
                     llvm_regex_char_t last) {
  if (p->error)
    return;
  size_t begin = 0;
  while (begin < set->count && set->ranges[begin].last + 1 < first)
    ++begin;
  size_t end = begin;
  while (end < set->count && set->ranges[end].first <= last + 1) {
    if (set->ranges[end].first < first)
      first = set->ranges[end].first;
    if (set->ranges[end].last > last)
      last = set->ranges[end].last;
    ++end;
  }
  if (begin == end && set->count == set->capacity) {
    if (set->capacity > (SIZE_MAX / sizeof(regex_range) - 8) / 2) {
      SETERROR(REG_ESPACE);
      return;
    }
    size_t capacity = set->capacity * 2 + 8;
    regex_range *next = realloc(set->ranges, capacity * sizeof(regex_range));
    if (!next) {
      SETERROR(REG_ESPACE);
      return;
    }
    set->ranges = next;
    set->capacity = capacity;
  }
  memmove(set->ranges + begin + 1, set->ranges + end,
          (set->count - end) * sizeof(regex_range));
  set->ranges[begin] = (regex_range){first, last};
  set->count = set->count - (end - begin) + 1;
}

static void freeset(struct parse *p, cset *set) {
  if (set->custom.destroy)
    set->custom.destroy(set->custom.context);
  free(set->ranges);
  memset(set, 0, sizeof(*set));
  if (set == &p->g->sets[p->g->ncsets - 1])
    --p->g->ncsets;
}

static int freezeset(struct parse *p, cset *set) {
  if (set->custom.contains)
    return (int)(set - p->g->sets);
  for (cset *other = p->g->sets; other < set; ++other)
    if (!other->custom.contains && other->count == set->count &&
        other->classes == set->classes && other->fold == set->fold &&
        other->inverted == set->inverted &&
        other->exclude_newline == set->exclude_newline &&
        (!set->count || !memcmp(other->ranges, set->ranges,
                               set->count * sizeof(regex_range)))) {
      freeset(p, set);
      return (int)(other - p->g->sets);
    }
  return (int)(set - p->g->sets);
}

static int firstch(struct parse *p, cset *set) {
  assert(!set->inverted && set->count);
  return set->ranges[0].first;
}

/* Only the singleton distinction is needed by the strip optimizer. */
static int nch(struct parse *p, cset *set) {
  if (set->custom.contains || set->inverted || set->classes || set->fold || set->count > 1)
    return 2;
  if (!set->count)
    return 0;
  return set->ranges[0].first == set->ranges[0].last ? 1 : 2;
}

/*
 - dupl - emit a duplicate of a bunch of sops
 */
static sopno                       /* start of duplicate */
dupl(struct parse *p, sopno start, /* from here */
     sopno finish)                 /* to this less one */
{
  sopno ret = HERE();
  sopno len = finish - start;

  if (p->error)
    return ret;
  assert(finish >= start);
  if (len == 0)
    return (ret);
  if (len > LONG_MAX - p->ssize) {
    SETERROR(REG_ESPACE);
    return ret;
  }
  enlarge(p, p->ssize + len); /* this many unexpected additions */
  if (p->error)
    return ret;
  assert(p->ssize >= p->slen + len);
  (void)memmove((char *)(p->strip + p->slen), (char *)(p->strip + start),
                (size_t)len * sizeof(sop));
  p->slen += len;
  return (ret);
}

/*
 - doemit - emit a strip operator
 *
 * It might seem better to implement this as a macro with a function as
 * hard-case backup, but it's just too big and messy unless there are
 * some changes to the data structures.  Maybe later.
 */
static void doemit(struct parse *p, sop op, size_t opnd) {
  /* avoid making error situations worse */
  if (p->error != 0)
    return;

  /* deal with oversize operands ("can't happen", more or less) */
  if (opnd >= (size_t)1 << OPSHIFT) {
    SETERROR(REG_ESPACE);
    return;
  }

  /* deal with undersized strip */
  if (p->slen >= p->ssize) {
    if (p->ssize >= LONG_MAX / 3 * 2) {
      SETERROR(REG_ESPACE);
      return;
    }
    enlarge(p, (p->ssize + 1) / 2 * 3); /* +50% */
    if (p->error)
      return;
  }
  assert(p->slen < p->ssize);

  /* finally, it's all reduced to the easy case */
  p->strip[p->slen++] = SOP(op, opnd);
}

/*
 - doinsert - insert a sop into the strip
 */
static void doinsert(struct parse *p, sop op, size_t opnd, sopno pos) {
  sopno sn;
  sop s;
  int i;

  /* avoid making error situations worse */
  if (p->error != 0)
    return;

  sn = HERE();
  EMIT(op, opnd); /* do checks, ensure space */
  if (p->error)
    return;
  assert(HERE() == sn + 1);
  s = p->strip[sn];

  /* adjust paren pointers */
  assert(pos > 0);
  for (i = 1; i < NPAREN; i++) {
    if (p->pbegin[i] >= pos) {
      p->pbegin[i]++;
    }
    if (p->pend[i] >= pos) {
      p->pend[i]++;
    }
  }

  memmove((char *)&p->strip[pos + 1], (char *)&p->strip[pos],
          (HERE() - pos - 1) * sizeof(sop));
  p->strip[pos] = s;
}

/*
 - dofwd - complete a forward reference
 */
static void dofwd(struct parse *p, sopno pos, sop value) {
  /* avoid making error situations worse */
  if (p->error != 0)
    return;

  if (value >= (sop)1 << OPSHIFT) {
    SETERROR(REG_ESPACE);
    return;
  }
  p->strip[pos] = OP(p->strip[pos]) | value;
}

/*
 - enlarge - enlarge the strip
 */
static void enlarge(struct parse *p, sopno size) {
  sop *sp;

  if (p->ssize >= size)
    return;

  if ((uintptr_t)size > SIZE_MAX / sizeof(sop)) {
    SETERROR(REG_ESPACE);
    return;
  }

  sp = (sop *)realloc(p->strip, size * sizeof(sop));
  if (sp == NULL) {
    SETERROR(REG_ESPACE);
    return;
  }
  p->strip = sp;
  p->ssize = size;
}

/*
 - stripsnug - compact the strip
 */
static void stripsnug(struct parse *p, struct re_guts *g) {
  g->nstates = p->slen;
  if ((uintptr_t)p->slen > SIZE_MAX / sizeof(sop)) {
    g->strip = p->strip;
    SETERROR(REG_ESPACE);
    return;
  }

  g->strip = (sop *)realloc((char *)p->strip, p->slen * sizeof(sop));
  if (g->strip == NULL) {
    SETERROR(REG_ESPACE);
    g->strip = p->strip;
  }
}

/*
 - findmust - fill in must and mlen with longest mandatory literal string
 *
 * This algorithm could do fancy things like analyzing the operands of |
 * for common subsequences.  Someday.  This code is simple and finds most
 * of the interesting cases.
 *
 * Note that must and mlen got initialized during setup.
 */
static void findmust(struct parse *p, struct re_guts *g) {
  sop *scan;
  sop *start = 0;    /* start initialized in the default case, after that */
  sop *newstart = 0; /* newstart was initialized in the OCHAR case */
  sopno newlen;
  sop s;
  llvm_regex_char_t *cp;
  sopno i;

  /* avoid making error situations worse */
  if (p->error != 0)
    return;

  /* find the longest OCHAR sequence in strip */
  newlen = 0;
  scan = g->strip + 1;
  do {
    s = *scan++;
    switch (OP(s)) {
    case OCHAR:        /* sequence member */
      if (newlen == 0) /* new sequence */
        newstart = scan - 1;
      newlen++;
      break;
    case OPLUS_: /* things that don't break one */
    case OLPAREN:
    case ORPAREN:
      break;
    case OQUEST_: /* things that must be skipped */
    case OCH_:
      scan--;
      do {
        scan += OPND(s);
        s = *scan;
        /* assert() interferes w debug printouts */
        if (OP(s) != O_QUEST && OP(s) != O_CH && OP(s) != OOR2) {
          g->iflags |= REGEX_BAD;
          return;
        }
      } while (OP(s) != O_QUEST && OP(s) != O_CH);
      REGEX_FALLTHROUGH;
    default:                  /* things that break a sequence */
      if (newlen > g->mlen) { /* ends one */
        start = newstart;
        g->mlen = newlen;
      }
      newlen = 0;
      break;
    }
  } while (OP(s) != OEND);

  if (g->mlen == 0) /* there isn't one */
    return;

  /* turn it into a character string */
  g->must = malloc(((size_t)g->mlen + 1) * sizeof(*g->must));
  if (g->must == NULL) { /* argh; just forget it */
    g->mlen = 0;
    return;
  }
  cp = g->must;
  scan = start;
  for (i = g->mlen; i > 0; i--) {
    while (OP(s = *scan++) != OCHAR)
      continue;
    assert(cp < g->must + g->mlen);
    *cp++ = (llvm_regex_char_t)OPND(s);
  }
  assert(cp == g->must + g->mlen);
  *cp++ = '\0'; /* just on general principles */
}

/*
 - pluscount - count + nesting
 */
static sopno /* nesting depth */
pluscount(struct parse *p, struct re_guts *g) {
  sop *scan;
  sop s;
  sopno plusnest = 0;
  sopno maxnest = 0;

  if (p->error != 0)
    return (0); /* there may not be an OEND */

  scan = g->strip + 1;
  do {
    s = *scan++;
    switch (OP(s)) {
    case OPLUS_:
      plusnest++;
      break;
    case O_PLUS:
      if (plusnest > maxnest)
        maxnest = plusnest;
      plusnest--;
      break;
    }
  } while (OP(s) != OEND);
  if (plusnest != 0)
    g->iflags |= REGEX_BAD;
  return (maxnest);
}
