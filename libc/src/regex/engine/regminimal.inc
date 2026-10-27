/*===-- Preference histories for the libc BSD regex strip ----------------===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*/

/* Unlike a first-success backtracker, this executor keeps the successful
 * histories at the earliest start. Minimal occurrences constrain those
 * histories before whole-match length and greedy subexpression preference.
 * The ordinary matcher remains the path for non-minimal patterns.
 * All traversal storage is fallible heap storage, not recursive C frames.
 */
typedef struct {
  size_t tag;
  llvm_regoff_t begin, end;
} regex_visit;

typedef struct regex_path {
  struct regex_path *next;
  sopno pc;
  llvm_regoff_t pos;
  llvm_regmatch_t *captures;
  llvm_regoff_t *loops;
  regex_visit *visits;
  size_t count, capacity;
} regex_path;

static void path_free(regex_path *path) {
  free(path->captures);
  free(path->loops);
  free(path->visits);
  free(path);
}

static void paths_free(regex_path *path) {
  while (path) {
    regex_path *next = path->next;
    path_free(path);
    path = next;
  }
}

static void path_queue(regex_path **pending, regex_path *path) {
  while (*pending && (*pending)->pos <= path->pos)
    pending = &(*pending)->next;
  path->next = *pending;
  *pending = path;
}

static regex_path *path_new(const struct re_guts *g, const regex_path *from) {
  if (g->nsub >= SIZE_MAX / sizeof(llvm_regmatch_t) ||
      (size_t)g->nstates > SIZE_MAX / sizeof(llvm_regoff_t))
    return NULL;
  regex_path *path = calloc(1, sizeof(*path));
  if (!path)
    return NULL;
  size_t capture_bytes = (g->nsub + 1) * sizeof(*path->captures);
  size_t loop_bytes = g->nstates * sizeof(*path->loops);
  path->captures = malloc(capture_bytes);
  path->loops = malloc(loop_bytes);
  if (!path->captures || !path->loops) {
    path_free(path);
    return NULL;
  }
  if (from) {
    path->pc = from->pc;
    path->pos = from->pos;
    memmove(path->captures, from->captures, capture_bytes);
    memmove(path->loops, from->loops, loop_bytes);
    if (from->count) {
      path->visits = malloc(from->count * sizeof(*path->visits));
      if (!path->visits) {
        path_free(path);
        return NULL;
      }
      path->capacity = path->count = from->count;
      memmove(path->visits, from->visits,
              from->count * sizeof(*path->visits));
    }
  } else {
    for (size_t i = 0; i <= g->nsub; ++i)
      path->captures[i] = (llvm_regmatch_t){-1, -1};
    for (sopno i = 0; i < g->nstates; ++i)
      path->loops[i] = -1;
  }
  return path;
}

static int path_branch(const struct re_guts *g, regex_path *path, sopno pc,
                       regex_path **pending) {
  regex_path *branch = path_new(g, path);
  if (!branch)
    return 0;
  branch->pc = pc;
  path_queue(pending, branch);
  return 1;
}

static int path_visit(regex_path *path, size_t tag) {
  if (path->count == path->capacity) {
    if (path->capacity > (SIZE_MAX / sizeof(regex_visit) - 8) / 2)
      return 0;
    size_t capacity = path->capacity * 2 + 8;
    regex_visit *visits = realloc(path->visits, capacity * sizeof(*visits));
    if (!visits)
      return 0;
    path->visits = visits;
    path->capacity = capacity;
  }
  path->visits[path->count++] = (regex_visit){tag, path->pos, -1};
  return 1;
}

/* A branch which does not use a minimal repetition is not constrained by it.
 * Nor does a shorter history add fictitious repetitions: compare only common
 * occurrences. In particular, a repeated zero-width minimum must not become
 * a preference for the fewest outer iterations.
 */
static int visit_compare(const regex_path *a, const regex_path *b, size_t tag,
                         int minimal, int order_prefixes) {
  size_t i = 0, j = 0;
  for (;;) {
    while (i < a->count && a->visits[i].tag != tag)
      ++i;
    while (j < b->count && b->visits[j].tag != tag)
      ++j;
    if (i == a->count || j == b->count) {
      if ((minimal && !order_prefixes) ||
          (i == a->count && j == b->count))
        return 0;
      return i == a->count ? 1 : -1;
    }
    llvm_regoff_t x = a->visits[i].end - a->visits[i].begin;
    llvm_regoff_t y = b->visits[j].end - b->visits[j].begin;
    if (x != y)
      return (x < y) == minimal ? -1 : 1;
    ++i;
    ++j;
  }
}

static int path_preferred(const struct re_guts *g, const size_t *order,
                          const regex_path *a, const regex_path *b) {
  if (a->pos != b->pos)
    return a->pos > b->pos;
  for (size_t i = 0; i < g->ntags; ++i) {
    size_t tag = order[i];
    if (!g->tags[tag].minimal) {
      int comparison = visit_compare(a, b, tag, 0, 1);
      if (comparison)
        return comparison < 0;
    }
  }
  return 0;
}

static int inside_minimal(const struct re_guts *g, const regex_path *path) {
  for (size_t i = path->count; i; --i)
    if (path->visits[i - 1].end == -1 &&
        g->tags[path->visits[i - 1].tag].minimal)
      return 1;
  return 0;
}

/* With one minimal column, an existing complete match is a permanent bound:
 * no higher-priority column can subsequently remove that witness. An open
 * visit's current length is only a lower bound, never a completed occurrence.
 */
static int exceeds_minimum(const regex_path *path, const regex_path *bound,
                           size_t tag) {
  size_t i = 0, j = 0;
  for (;;) {
    while (i < path->count && path->visits[i].tag != tag)
      ++i;
    while (j < bound->count && bound->visits[j].tag != tag)
      ++j;
    if (i == path->count || j == bound->count)
      return 0;
    const regex_visit *visit = &path->visits[i];
    llvm_regoff_t length = (visit->end < 0 ? path->pos : visit->end) - visit->begin;
    llvm_regoff_t limit = bound->visits[j].end - bound->visits[j].begin;
    if (length != limit)
      return length > limit;
    if (visit->end < 0)
      return 0;
    ++i;
    ++j;
  }
}

/* Only discard a path when its preference cannot change under an identical
 * continuation. A proper history prefix is incomparable: later iterations
 * might extend it. Open intervals must have the same status in both paths.
 */
static int stable_visit_compare(const regex_path *a, const regex_path *b,
                                size_t tag, int minimal) {
  size_t i = 0, j = 0;
  for (;;) {
    while (i < a->count && a->visits[i].tag != tag)
      ++i;
    while (j < b->count && b->visits[j].tag != tag)
      ++j;
    if (i == a->count || j == b->count)
      return i == a->count && j == b->count ? 0 : 2;
    const regex_visit *x = &a->visits[i], *y = &b->visits[j];
    if ((x->end < 0) != (y->end < 0))
      return 2;
    llvm_regoff_t xn = (x->end < 0 ? a->pos : x->end) - x->begin;
    llvm_regoff_t yn = (y->end < 0 ? b->pos : y->end) - y->begin;
    if (xn != yn)
      return (xn < yn) == minimal ? -1 : 1;
    ++i;
    ++j;
  }
}

static int stable_path_compare(const struct re_guts *g, const size_t *order,
                               const regex_path *a, const regex_path *b) {
  for (sopno i = g->firststate + 1; i < a->pc; ++i)
    if (OP(g->strip[i]) == OPLUS_ && i + (sopno)OPND(g->strip[i]) >= a->pc &&
        a->loops[i] != b->loops[i])
      return 2;
  for (int minimal = 1; minimal >= 0; --minimal)
    for (size_t i = 0; i < g->ntags; ++i) {
      size_t tag = order[i];
      if (g->tags[tag].minimal == minimal) {
        int comparison = stable_visit_compare(a, b, tag, minimal);
        if (comparison)
          return comparison;
      }
    }
  // Equal preferences need not imply equal captures inside nullable loops.
  return memcmp(a->captures, b->captures,
                (g->nsub + 1) * sizeof(*a->captures)) == 0 ? 0 : 2;
}

/* Retain incomparable states. Backreferences bypass this merge because their
 * future language depends on captures, not just strip position and input.
 */
static int remember_path(const struct re_guts *g, const size_t *order,
                         regex_path *path, regex_path **seen) {
  regex_path **link = seen;
  while (*link) {
    regex_path *previous = *link;
    if (previous->pc == path->pc) {
      int comparison = stable_path_compare(g, order, path, previous);
      if (comparison == 0 || comparison == 1)
        return 0;
      if (comparison == -1) {
        *link = previous->next;
        path_free(previous);
        continue;
      }
    }
    link = &previous->next;
  }
  regex_path *copy = path_new(g, path);
  if (!copy)
    return -1;
  copy->next = *seen;
  *seen = copy;
  return 1;
}

static int minimal_match(struct re_guts *g, const llvm_regex_char_t *text,
                         size_t length, size_t nmatch,
                         llvm_regmatch_t pmatch[], int flags) {
  llvm_regoff_t begin = 0, end = length;
  if (flags & REG_STARTEND) {
    begin = pmatch[0].rm_so;
    end = pmatch[0].rm_eo;
  }
  /* Find the earliest viable start with the existing language recognizer.
   * Tags are epsilon transitions there; preference cannot change existence.
   */
  struct re_guts recognizer = *g;
  recognizer.cflags &= ~REG_NOSUB;
  llvm_regmatch_t extent = {begin, end};
  int result = g->nstates <= (long)(CHAR_BIT * sizeof(states1))
                   ? smatcher(&recognizer, text, length, 1, &extent, flags)
                   : lmatcher(&recognizer, text, length, 1, &extent, flags);
  if (result)
    return result;
  if ((g->cflags & REG_NOSUB) || !nmatch)
    return 0;

  size_t *order = malloc(g->ntags * sizeof(*order));
  regex_path *pending = NULL, *completed = NULL, *seen = NULL;
  llvm_regoff_t seen_position = -1;
  regex_path *path = path_new(g, NULL);
  if (!order || !path) {
    free(order);
    if (path)
      path_free(path);
    return REG_ESPACE;
  }
  for (size_t i = 0; i < g->ntags; ++i) {
    size_t j = i;
    while (j && g->tags[order[j - 1]].source > g->tags[i].source) {
      order[j] = order[j - 1];
      --j;
    }
    order[j] = i;
  }
  size_t minimal_count = 0, single_tag = 0;
  for (size_t i = 0; i < g->ntags; ++i)
    if (g->tags[i].minimal) {
      ++minimal_count;
      single_tag = i;
    }
  regex_path *bound = NULL;
  path->pc = g->firststate + 1;
  path->pos = extent.rm_so;
  path->captures[0].rm_so = extent.rm_so;
  result = 0;
  for (;;) {
    if (minimal_count == 1 && bound && exceeds_minimum(path, bound, single_tag))
      goto failed_path;
    if (path->pos != seen_position) {
      paths_free(seen);
      seen = NULL;
      seen_position = path->pos;
    }
    if (!g->backrefs && OP(g->strip[path->pc - 1]) == OPLUS_) {
      int remembered = remember_path(g, order, path, &seen);
      if (remembered < 0)
        goto exhausted;
      if (!remembered)
        goto failed_path;
    }
    llvm_regoff_t old_position = path->pos;
    sop instruction = g->strip[path->pc];
    size_t operand = OPND(instruction);
    switch (OP(instruction)) {
    case OEND:
      path->captures[0].rm_eo = path->pos;
      if (!bound || visit_compare(path, bound, single_tag, 1, 1) < 0)
        bound = path;
      path->next = completed;
      completed = path;
      path = NULL;
      goto next_path;
    case OCHAR:
      if (path->pos == end || text[path->pos] != (llvm_regex_char_t)operand)
        goto failed_path;
      ++path->pos;
      break;
    case OANY:
      if (path->pos == end)
        goto failed_path;
      ++path->pos;
      break;
    case OANYOF:
      if (path->pos == end || !CHIN(&g->sets[operand], text[path->pos]))
        goto failed_path;
      ++path->pos;
      break;
    case OBOL:
      if (!((path->pos == begin && !(flags & REG_NOTBOL)) ||
            (path->pos > begin && text[path->pos - 1] == '\n' &&
             (g->cflags & REG_NEWLINE))))
        goto failed_path;
      break;
    case OEOL:
      if (!((path->pos == end && !(flags & REG_NOTEOL)) ||
            (path->pos < end && text[path->pos] == '\n' &&
             (g->cflags & REG_NEWLINE))))
        goto failed_path;
      break;
    case OBOW:
      if (!(path->pos < end && regex_word(g, text[path->pos]) &&
            ((path->pos == begin && !(flags & REG_NOTBOL)) ||
             (path->pos > begin && !regex_word(g, text[path->pos - 1])))))
        goto failed_path;
      break;
    case OEOW:
      if (!(path->pos > begin && regex_word(g, text[path->pos - 1]) &&
            ((path->pos == end && !(flags & REG_NOTEOL)) ||
             (path->pos < end && !regex_word(g, text[path->pos])))))
        goto failed_path;
      break;
    case OLPAREN:
      path->captures[operand].rm_so = path->pos;
      path->captures[operand].rm_eo = -1;
      // A fresh iteration must not retain a nested capture from an alternative
      // used only in an earlier iteration of this parenthesized expression.
      for (sopno pc = path->pc + 1;
           !(OP(g->strip[pc]) == ORPAREN && OPND(g->strip[pc]) == operand);
           ++pc)
        if (OP(g->strip[pc]) == OLPAREN && OPND(g->strip[pc]) > operand)
          path->captures[OPND(g->strip[pc])] = (llvm_regmatch_t){-1, -1};
      break;
    case ORPAREN:
      path->captures[operand].rm_eo = path->pos;
      break;
    case OBACK_: {
      llvm_regmatch_t capture = path->captures[operand];
      if (capture.rm_so < 0 || capture.rm_eo < capture.rm_so ||
          capture.rm_eo - capture.rm_so > end - path->pos)
        goto failed_path;
      for (llvm_regoff_t i = capture.rm_so; i < capture.rm_eo; ++i)
        if (!regex_equal(g, text[i], text[path->pos++]))
          goto failed_path;
      size_t nesting = 1;
      while (nesting) {
        sop op = OP(g->strip[++path->pc]);
        if (op == OBACK_)
          ++nesting;
        else if (op == O_BACK)
          --nesting;
      }
      break;
    }
    case OTAG_:
      if (!path_visit(path, operand))
        goto exhausted;
      break;
    case O_TAG: {
      size_t i = path->count;
      while (i && (path->visits[i - 1].tag != operand ||
                   path->visits[i - 1].end != -1))
        --i;
      if (!i) {
        result = REG_ASSERT;
        goto cleanup;
      }
      path->visits[i - 1].end = path->pos;
      break;
    }
    case OPLUS_:
      path->loops[path->pc] = path->pos;
      break;
    case O_PLUS: {
      sopno start = path->pc - operand;
      if (path->loops[start] != path->pos) {
        path->loops[start] = path->pos;
        if (inside_minimal(g, path)) {
          if (!path_branch(g, path, start + 1, &pending))
            goto exhausted;
        } else {
          if (!path_branch(g, path, path->pc + 1, &pending))
            goto exhausted;
          path->pc = start;
        }
      }
      break;
    }
    case OQUEST_:
      if (inside_minimal(g, path)) {
        if (!path_branch(g, path, path->pc + 1, &pending))
          goto exhausted;
        path->pc += operand;
      } else if (!path_branch(g, path, path->pc + operand + 1, &pending)) {
        goto exhausted;
      }
      break;
    case OCH_:
    case OOR2:
      if (OP(g->strip[path->pc + operand]) != O_CH &&
          !path_branch(g, path, path->pc + operand, &pending))
        goto exhausted;
      break;
    case OOR1:
      ++path->pc;
      while (OP(g->strip[path->pc]) != O_CH)
        path->pc += OPND(g->strip[path->pc]);
      break;
    case O_QUEST:
    case O_CH:
      break;
    default:
      result = REG_ASSERT;
      goto cleanup;
    }
    ++path->pc;
    if (path->pos != old_position && pending && pending->pos < path->pos) {
      path_queue(&pending, path);
      path = NULL;
      goto next_path;
    }
    continue;
  failed_path:
    path_free(path);
    path = NULL;
  next_path:
    if (!pending)
      break;
    path = pending;
    pending = path->next;
    path->next = NULL;
  }

  /* A lexicographic minimum with extensions before prefixes represents all
   * comparisons in a column. The final comparison still treats prefixes and
   * unused alternatives as incomparable. This avoids pairwise candidate scans.
   */
  for (size_t i = 0; i < g->ntags; ++i) {
    size_t tag = order[i];
    if (!g->tags[tag].minimal)
      continue;
    regex_path *minimum = completed;
    for (regex_path *candidate = completed; candidate; candidate = candidate->next)
      if (visit_compare(candidate, minimum, tag, 1, 1) < 0)
        minimum = candidate;
    regex_path **link = &completed;
    while (*link) {
      regex_path *candidate = *link;
      if (visit_compare(minimum, candidate, tag, 1, 0) < 0) {
        *link = candidate->next;
        path_free(candidate);
      } else {
        link = &candidate->next;
      }
    }
  }
  regex_path *best = completed;
  for (regex_path *candidate = completed; candidate; candidate = candidate->next)
    if (path_preferred(g, order, candidate, best))
      best = candidate;
  if (!best) {
    result = REG_NOMATCH;
    goto cleanup;
  }
  if (!(g->cflags & REG_NOSUB))
    for (size_t i = 0; i < nmatch; ++i)
      pmatch[i] = i <= g->nsub ? best->captures[i] : (llvm_regmatch_t){-1, -1};
  goto cleanup;
exhausted:
  result = REG_ESPACE;
cleanup:
  if (path)
    path_free(path);
  paths_free(pending);
  paths_free(seen);
  paths_free(completed);
  free(order);
  return result;
}
