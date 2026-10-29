//===-- Shared startup security policy tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/startup_fds.h"
#include "src/__support/OSUtil/linux/startup_security.h"
#include <assert.h>
#include <string.h>

using namespace LIBC_NAMESPACE::startup_security;

struct Descriptors {
  unsigned missing = 7, opens = 0, stats = 0, closes = 0;
  unsigned interrupts = 0;
  long query_error = 0, open_error = 0, stat_error = 0;
  bool wrong_slot = false, wrong_type = false, wrong_device = false,
       missing_type = false;
  int pending = -1;
  long call(long n, long a, long b, long c, long d, long e, long) {
    unsigned interrupted = n == SYS_fcntl    ? 1U
                           : n == SYS_openat ? 2U
                           : n == SYS_statx  ? 4U
                                             : 0U;
    if (interrupts & interrupted) {
      interrupts &= ~interrupted;
      return -EINTR;
    }
    if (n == SYS_fcntl) {
      assert(a >= 0 && a < 3 && b == F_GETFD);
      return query_error ? query_error : (missing & (1U << a)) ? -EBADF : 0;
    }
    if (n == SYS_openat) {
      ++opens;
      assert(a == AT_FDCWD && d == 0);
      for (pending = 0; pending < 3; ++pending)
        if (missing & (1U << pending))
          break;
      assert(pending < 3);
      assert(!strcmp(reinterpret_cast<const char *>(b),
                     pending == 0 ? "/dev/full" : "/dev/null"));
      assert(c ==
             ((pending == 0 ? O_WRONLY : O_RDONLY) | O_NOFOLLOW | O_NONBLOCK));
      if (open_error)
        return open_error;
      missing &= ~(1U << pending);
      return wrong_slot ? 9 : pending;
    }
    if (n == SYS_statx) {
      ++stats;
      assert(a == pending && !*reinterpret_cast<const char *>(b));
      assert(c == AT_EMPTY_PATH && d == STATX_TYPE);
      if (stat_error)
        return stat_error;
      auto *s = reinterpret_cast<struct statx *>(e);
      s->stx_mask = missing_type ? 0 : STATX_TYPE;
      s->stx_mode = wrong_type ? S_IFREG : S_IFCHR;
      s->stx_rdev_major = wrong_device ? 2 : 1;
      s->stx_rdev_minor = pending == 0 ? 7 : 3;
      return 0;
    }
    assert(n == SYS_close && a == (wrong_slot ? 9 : pending));
    ++closes;
    return -EINTR;
  }
  long protect() {
    return protect_standard_fds(
        [&](long n, long a, long b, long c, long d, long e, long f) {
          return call(n, a, b, c, d, e, f);
        });
  }
};

extern "C" int main() {
  const uintptr_t tags[] = {AT_SECURE, AT_UID, AT_EUID, AT_GID, AT_EGID};
  for (unsigned missing = 0; missing != 6; ++missing) {
    Inputs input;
    assert(input.secure());
    for (unsigned i = 0; i != 5; ++i)
      if (i != missing)
        assert(input.observe(tags[i], 0));
    assert(input.complete() == (missing == 5));
    assert(input.secure() == (missing != 5));
  }
  for (unsigned changed = 0; changed != 5; ++changed) {
    Inputs input;
    for (unsigned i = 0; i != 5; ++i)
      assert(input.observe(tags[i], changed == i ? 1234 : 0));
    assert(input.complete() && input.secure());
    assert(!input.observe(tags[changed], 0));
    assert(!input.complete() && input.secure());
  }
  Inputs invalid;
  assert(!invalid.observe(AT_UID, uintptr_t(UINT32_MAX) + 1));
  assert(invalid.secure());

  const char *env[] = {"LD_LIBRARY_PATH=/bad",
                       "LANG=C.UTF-8",
                       "PATH=/bin",
                       "LD_LIBRARY_PATH=/worse",
                       "NLSPATH=",
                       "LC_ALL=C",
                       "LD_FUTURE=x",
                       "TMPDIR=/bad",
                       "LOCPATH=/bad",
                       "GLIBC_TUNABLES=",
                       "TZDIR=/bad",
                       "GCONV_PATH=/bad"};
  uintptr_t words[64]{};
  for (unsigned i = 0; i < sizeof(env) / sizeof(*env); ++i)
    words[i] = reinterpret_cast<uintptr_t>(env[i]);
  auto *old_aux = words + sizeof(env) / sizeof(*env) + 1;
  const uintptr_t expected_aux[] = {
      AT_SECURE, 1, AT_UID, 1234, AT_EUID, 0, 0xabcdef, 0x123456, AT_NULL, 0};
  memcpy(old_aux, expected_aux, sizeof(expected_aux));
  auto *aux = sanitize(reinterpret_cast<char **>(words), old_aux);
  assert(aux == words + 5);
  assert(words[0] == reinterpret_cast<uintptr_t>(env[1]));
  assert(words[1] == reinterpret_cast<uintptr_t>(env[2]));
  assert(words[2] == reinterpret_cast<uintptr_t>(env[5]));
  assert(words[3] == reinterpret_cast<uintptr_t>(env[6]));
  assert(!words[4] && !memcmp(aux, expected_aux, sizeof(expected_aux)));
  assert(sanitize(reinterpret_cast<char **>(words), aux) == aux);
  assert(!memcmp(aux, expected_aux, sizeof(expected_aux)));
  assert(dangerous("LD_PRELOAD") && dangerous("LD_PRELOAD="));
  assert(!dangerous("LD_PRELOAD_MORE=x") && !dangerous("LD_"));
  assert(!dangerous("L") && !dangerous(""));

  for (unsigned missing = 0; missing != 8; ++missing) {
    Descriptors model;
    model.missing = missing;
    assert(!model.protect());
    assert(!model.missing && !model.closes);
    assert(model.opens == unsigned(__builtin_popcount(missing)));
    assert(model.stats == model.opens);
  }
  Descriptors model;
  model.interrupts = 7;
  assert(!model.protect() && !model.interrupts);
  assert(!model.missing && model.opens == 3 && model.stats == 3 &&
         !model.closes);
  model = {};
  model.query_error = -EIO;
  assert(model.protect() == -EIO && !model.opens);
  model = {};
  model.open_error = -ELOOP;
  assert(model.protect() == -ELOOP && !model.stats && !model.closes);
  model = {};
  model.stat_error = -EIO;
  assert(model.protect() == -EIO && model.closes == 1);
  model = {};
  model.wrong_slot = true;
  assert(model.protect() == -EBADF && !model.stats && model.closes == 1);
  for (unsigned fault = 0; fault != 3; ++fault) {
    model = {};
    model.wrong_type = fault == 0;
    model.wrong_device = fault == 1;
    model.missing_type = fault == 2;
    assert(model.protect() == -ENODEV && model.closes == 1);
  }
  return 0;
}
