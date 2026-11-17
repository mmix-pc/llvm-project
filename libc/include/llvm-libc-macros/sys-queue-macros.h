//===-- Macros defined in sys/queue.h header file -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_SYS_QUEUE_MACROS_H
#define LLVM_LIBC_MACROS_SYS_QUEUE_MACROS_H

#include "containerof-macro.h"
#include "null-macro.h"

#ifdef __cplusplus
#define QUEUE_TYPEOF(type) type
#else
#define QUEUE_TYPEOF(type) struct type
#endif

// Singly-linked list definitions.

#define SLIST_HEAD(name, type)                                                 \
  struct name {                                                                \
    struct type *slh_first;                                                    \
  }

#define SLIST_CLASS_HEAD(name, type)                                           \
  struct name {                                                                \
    class type *slh_first;                                                     \
  }

#define SLIST_HEAD_INITIALIZER(head) {NULL}

#define SLIST_ENTRY(type)                                                      \
  struct {                                                                     \
    struct type *next;                                                         \
  }

#define SLIST_CLASS_ENTRY(type)                                                \
  struct {                                                                     \
    class type *next;                                                          \
  }

// Singly-linked list access methods.

#define SLIST_EMPTY(head) ((head)->slh_first == NULL)
#define SLIST_FIRST(head) ((head)->slh_first)
#define SLIST_NEXT(elem, field) ((elem)->field.next)

#define SLIST_FOREACH(var, head, field)                                        \
  for ((var) = SLIST_FIRST(head); (var); (var) = SLIST_NEXT(var, field))

#define SLIST_FOREACH_FROM(var, head, field)                                   \
  if (!(var))                                                                  \
    (var) = SLIST_FIRST(head);                                                 \
  for (; (var); (var) = SLIST_NEXT(var, field))

#define SLIST_FOREACH_SAFE(var, head, field, tvar)                             \
  for ((var) = SLIST_FIRST(head);                                              \
       (var) && ((tvar) = SLIST_NEXT(var, field), 1); (var) = (tvar))

#define SLIST_FOREACH_FROM_SAFE(var, head, field, tvar)                        \
  if (!(var))                                                                  \
    (var) = SLIST_FIRST(head);                                                 \
  for (; (var) && ((tvar) = SLIST_NEXT(var, field), 1); (var) = (tvar))

// Singly-linked list functions.

#define SLIST_CONCAT(head1, head2, type, field)                                \
  do {                                                                         \
    if (SLIST_EMPTY(head1)) {                                                  \
      if ((SLIST_FIRST(head1) = SLIST_FIRST(head2)) != NULL)                   \
        SLIST_INIT(head2);                                                     \
    } else if (!SLIST_EMPTY(head2)) {                                          \
      QUEUE_TYPEOF(type) *cur = SLIST_FIRST(head1);                            \
      while (SLIST_NEXT(cur, field) != NULL)                                   \
        cur = SLIST_NEXT(cur, field);                                          \
      SLIST_NEXT(cur, field) = SLIST_FIRST(head2);                             \
      SLIST_INIT(head2);                                                       \
    }                                                                          \
  } while (0)

#define SLIST_INIT(head)                                                       \
  do {                                                                         \
    SLIST_FIRST(head) = NULL;                                                  \
  } while (0)

#define SLIST_INSERT_AFTER(slistelem, elem, field)                             \
  do {                                                                         \
    SLIST_NEXT(elem, field) = SLIST_NEXT(slistelem, field);                    \
    SLIST_NEXT(slistelem, field) = (elem);                                     \
  } while (0)

#define SLIST_INSERT_HEAD(head, elem, field)                                   \
  do {                                                                         \
    SLIST_NEXT(elem, field) = SLIST_FIRST(head);                               \
    SLIST_FIRST(head) = (elem);                                                \
  } while (0)

#define SLIST_REMOVE(head, elem, type, field)                                  \
  do {                                                                         \
    if (SLIST_FIRST(head) == (elem)) {                                         \
      SLIST_REMOVE_HEAD(head, field);                                          \
    } else {                                                                   \
      QUEUE_TYPEOF(type) *cur = SLIST_FIRST(head);                             \
      while (SLIST_NEXT(cur, field) != (elem))                                 \
        cur = SLIST_NEXT(cur, field);                                          \
      SLIST_REMOVE_AFTER(cur, field);                                          \
    }                                                                          \
  } while (0)

#define SLIST_REMOVE_AFTER(elem, field)                                        \
  do {                                                                         \
    SLIST_NEXT(elem, field) = SLIST_NEXT(SLIST_NEXT(elem, field), field);      \
  } while (0)

#define SLIST_REMOVE_HEAD(head, field)                                         \
  do {                                                                         \
    SLIST_FIRST(head) = SLIST_NEXT(SLIST_FIRST(head), field);                  \
  } while (0)

#define SLIST_SWAP(head1, head2, type)                                         \
  do {                                                                         \
    QUEUE_TYPEOF(type) *first = SLIST_FIRST(head1);                            \
    SLIST_FIRST(head1) = SLIST_FIRST(head2);                                   \
    SLIST_FIRST(head2) = first;                                                \
  } while (0)

// Singly-linked tail queue definitions.

#define STAILQ_HEAD(name, type)                                                \
  struct name {                                                                \
    struct type *stqh_first;                                                   \
    struct type **stqh_last;                                                   \
  }

#define STAILQ_CLASS_HEAD(name, type)                                          \
  struct name {                                                                \
    class type *stqh_first;                                                    \
    class type **stqh_last;                                                    \
  }

#define STAILQ_HEAD_INITIALIZER(head) {NULL, &(head).stqh_first}

#define STAILQ_ENTRY(type)                                                     \
  struct {                                                                     \
    struct type *next;                                                         \
  }

#define STAILQ_CLASS_ENTRY(type)                                               \
  struct {                                                                     \
    class type *next;                                                          \
  }

// Singly-linked tail queue access methods.

#define STAILQ_EMPTY(head) ((head)->stqh_first == NULL)
#define STAILQ_FIRST(head) ((head)->stqh_first)
#define STAILQ_LAST(head, type, field)                                         \
  (STAILQ_EMPTY(head)                                                          \
       ? NULL                                                                  \
       : __containerof((head)->stqh_last, QUEUE_TYPEOF(type), field.next))
#define STAILQ_NEXT(elem, field) ((elem)->field.next)

#define STAILQ_FOREACH(var, head, field)                                       \
  for ((var) = STAILQ_FIRST(head); (var); (var) = STAILQ_NEXT(var, field))

#define STAILQ_FOREACH_FROM(var, head, field)                                  \
  if (!(var))                                                                  \
    (var) = STAILQ_FIRST(head);                                                \
  for (; (var); (var) = STAILQ_NEXT(var, field))

#define STAILQ_FOREACH_SAFE(var, head, field, tvar)                            \
  for ((var) = STAILQ_FIRST(head);                                             \
       (var) && ((tvar) = STAILQ_NEXT(var, field), 1); (var) = (tvar))

#define STAILQ_FOREACH_FROM_SAFE(var, head, field, tvar)                       \
  if (!(var))                                                                  \
    (var) = STAILQ_FIRST(head);                                                \
  for (; (var) && ((tvar) = STAILQ_NEXT(var, field), 1); (var) = (tvar))

// Singly-linked tail queue functions.

#define STAILQ_CONCAT(head1, head2, type, field)                               \
  do {                                                                         \
    if (!STAILQ_EMPTY(head2)) {                                                \
      *(head1)->stqh_last = (head2)->stqh_first;                               \
      (head1)->stqh_last = (head2)->stqh_last;                                 \
      STAILQ_INIT(head2);                                                      \
    }                                                                          \
  } while (0)

#define STAILQ_INIT(head)                                                      \
  do {                                                                         \
    STAILQ_FIRST(head) = NULL;                                                 \
    (head)->stqh_last = &STAILQ_FIRST(head);                                   \
  } while (0)

#define STAILQ_INSERT_AFTER(head, listelem, elem, field)                       \
  do {                                                                         \
    if ((STAILQ_NEXT(elem, field) = STAILQ_NEXT(listelem, field)) == NULL)     \
      (head)->stqh_last = &STAILQ_NEXT(elem, field);                           \
    STAILQ_NEXT(listelem, field) = (elem);                                     \
  } while (0)

#define STAILQ_INSERT_HEAD(head, elem, field)                                  \
  do {                                                                         \
    if ((STAILQ_NEXT(elem, field) = STAILQ_FIRST(head)) == NULL)               \
      (head)->stqh_last = &STAILQ_NEXT(elem, field);                           \
    STAILQ_FIRST(head) = (elem);                                               \
  } while (0)

#define STAILQ_INSERT_TAIL(head, elem, field)                                  \
  do {                                                                         \
    STAILQ_NEXT(elem, field) = NULL;                                           \
    *(head)->stqh_last = (elem);                                               \
    (head)->stqh_last = &STAILQ_NEXT(elem, field);                             \
  } while (0)

#define STAILQ_REMOVE(head, elem, type, field)                                 \
  do {                                                                         \
    if (STAILQ_FIRST(head) == (elem)) {                                        \
      STAILQ_REMOVE_HEAD(head, field);                                         \
    } else {                                                                   \
      QUEUE_TYPEOF(type) *cur = STAILQ_FIRST(head);                            \
      while (STAILQ_NEXT(cur, field) != (elem))                                \
        cur = STAILQ_NEXT(cur, field);                                         \
      STAILQ_REMOVE_AFTER(head, cur, field);                                   \
    }                                                                          \
  } while (0)

#define STAILQ_REMOVE_AFTER(head, elem, field)                                 \
  do {                                                                         \
    if ((STAILQ_NEXT(elem, field) =                                            \
             STAILQ_NEXT(STAILQ_NEXT(elem, field), field)) == NULL)            \
      (head)->stqh_last = &STAILQ_NEXT(elem, field);                           \
  } while (0)

#define STAILQ_REMOVE_HEAD(head, field)                                        \
  do {                                                                         \
    if ((STAILQ_FIRST(head) = STAILQ_NEXT(STAILQ_FIRST(head), field)) == NULL) \
      (head)->stqh_last = &STAILQ_FIRST(head);                                 \
  } while (0)

#define STAILQ_SWAP(head1, head2, type)                                        \
  do {                                                                         \
    QUEUE_TYPEOF(type) *first = STAILQ_FIRST(head1);                           \
    QUEUE_TYPEOF(type) **last = (head1)->stqh_last;                            \
    STAILQ_FIRST(head1) = STAILQ_FIRST(head2);                                 \
    (head1)->stqh_last = (head2)->stqh_last;                                   \
    STAILQ_FIRST(head2) = first;                                               \
    (head2)->stqh_last = last;                                                 \
    if (STAILQ_EMPTY(head1))                                                   \
      (head1)->stqh_last = &STAILQ_FIRST(head1);                               \
    if (STAILQ_EMPTY(head2))                                                   \
      (head2)->stqh_last = &STAILQ_FIRST(head2);                               \
  } while (0)

// Doubly-linked tail queues. Each entry retains the address of the pointer
// which references it, allowing removal without a traversal.
#define TAILQ_HEAD(name, type)                                                 \
  struct name {                                                                \
    struct type *tqh_first;                                                    \
    struct type **tqh_last;                                                    \
  }
#define TAILQ_HEAD_INITIALIZER(head) {NULL, &(head).tqh_first}
#define TAILQ_ENTRY(type)                                                      \
  struct {                                                                     \
    struct type *tqe_next;                                                     \
    struct type **tqe_prev;                                                    \
  }
#define TAILQ_FIRST(head) ((head)->tqh_first)
#define TAILQ_NEXT(elem, field) ((elem)->field.tqe_next)
#define TAILQ_EMPTY(head) (TAILQ_FIRST(head) == NULL)
#define TAILQ_LAST(head, headname)                                             \
  (*(((struct headname *)((head)->tqh_last))->tqh_last))
#define TAILQ_PREV(elem, headname, field)                                      \
  (*(((struct headname *)((elem)->field.tqe_prev))->tqh_last))
#define TAILQ_INIT(head)                                                       \
  do {                                                                         \
    (head)->tqh_first = NULL;                                                  \
    (head)->tqh_last = &(head)->tqh_first;                                     \
  } while (0)
#define TAILQ_INSERT_HEAD(head, elem, field)                                   \
  do {                                                                         \
    (elem)->field.tqe_next = (head)->tqh_first;                                \
    if ((head)->tqh_first != NULL)                                             \
      (head)->tqh_first->field.tqe_prev = &(elem)->field.tqe_next;             \
    else                                                                       \
      (head)->tqh_last = &(elem)->field.tqe_next;                              \
    (head)->tqh_first = (elem);                                                \
    (elem)->field.tqe_prev = &(head)->tqh_first;                               \
  } while (0)
#define TAILQ_INSERT_TAIL(head, elem, field)                                   \
  do {                                                                         \
    (elem)->field.tqe_next = NULL;                                             \
    (elem)->field.tqe_prev = (head)->tqh_last;                                 \
    *(head)->tqh_last = (elem);                                                \
    (head)->tqh_last = &(elem)->field.tqe_next;                                \
  } while (0)
#define TAILQ_INSERT_AFTER(head, at, elem, field)                              \
  do {                                                                         \
    (elem)->field.tqe_next = (at)->field.tqe_next;                             \
    if ((elem)->field.tqe_next != NULL)                                        \
      (elem)->field.tqe_next->field.tqe_prev = &(elem)->field.tqe_next;        \
    else                                                                       \
      (head)->tqh_last = &(elem)->field.tqe_next;                              \
    (at)->field.tqe_next = (elem);                                             \
    (elem)->field.tqe_prev = &(at)->field.tqe_next;                            \
  } while (0)
#define TAILQ_INSERT_BEFORE(at, elem, field)                                   \
  do {                                                                         \
    (elem)->field.tqe_prev = (at)->field.tqe_prev;                             \
    (elem)->field.tqe_next = (at);                                             \
    *(at)->field.tqe_prev = (elem);                                            \
    (at)->field.tqe_prev = &(elem)->field.tqe_next;                            \
  } while (0)
#define TAILQ_REMOVE(head, elem, field)                                        \
  do {                                                                         \
    if ((elem)->field.tqe_next != NULL)                                        \
      (elem)->field.tqe_next->field.tqe_prev = (elem)->field.tqe_prev;         \
    else                                                                       \
      (head)->tqh_last = (elem)->field.tqe_prev;                               \
    *(elem)->field.tqe_prev = (elem)->field.tqe_next;                          \
  } while (0)
#define TAILQ_FOREACH(var, head, field)                                        \
  for ((var) = TAILQ_FIRST(head); (var) != NULL; (var) = TAILQ_NEXT(var, field))
#define TAILQ_FOREACH_SAFE(var, head, field, next)                             \
  for ((var) = TAILQ_FIRST(head);                                              \
       (var) != NULL && (((next) = TAILQ_NEXT(var, field)), 1);                \
       (var) = (next))

#endif // LLVM_LIBC_MACROS_SYS_QUEUE_MACROS_H
