//===-- Concurrent execution of an immutable regex ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/locale_macros.h"
#include "hdr/regex_macros.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/freelist_heap.h"
#include "src/locale/freelocale.h"
#include "src/locale/newlocale.h"
#include "src/locale/uselocale.h"
#include "src/pthread/pthread_create.h"
#include "src/pthread/pthread_join.h"
#include "src/regex/regcomp.h"
#include "src/regex/regexec.h"
#include "src/regex/regfree.h"
#include "test/UnitTest/Test.h"

namespace {
// The hermetic harness has an unsynchronized bump allocator. Serialize only
// test allocation, not regex execution, to avoid testing that harness race.
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
LIBC_NAMESPACE::cpp::Atomic<bool> allocating{false};
struct AllocationLock {
  AllocationLock() {
    while (
        allocating.exchange(true, LIBC_NAMESPACE::cpp::MemoryOrder::ACQUIRE)) {
    }
  }
  ~AllocationLock() {
    allocating.store(false, LIBC_NAMESPACE::cpp::MemoryOrder::RELEASE);
  }
};
} // namespace
extern "C" void *__wrap_malloc(size_t size) {
  AllocationLock lock;
  return heap.allocate(size);
}
extern "C" void *__wrap_realloc(void *p, size_t size) {
  AllocationLock lock;
  return heap.realloc(p, size);
}
extern "C" void __wrap_free(void *p) {
  AllocationLock lock;
  heap.free(p);
}

TEST(LlvmLibcRegexThreadTest, SharedExpressionHasPrivateExecutionState) {
  locale_t locale = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C.UTF-8", nullptr);
  ASSERT_NE(locale, static_cast<locale_t>(nullptr));
  locale_t previous = LIBC_NAMESPACE::uselocale(locale);
  regex_t re;
  ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, "^\\([[:alpha:]]*\\)\\1.$", 0), 0);
  LIBC_NAMESPACE::cpp::Atomic<bool> start{false};
  struct State {
    locale_t locale;
    const regex_t *re;
    LIBC_NAMESPACE::cpp::Atomic<bool> *start;
    bool passed = false;
  } states[4];
  auto worker = [](void *argument) -> void * {
    auto &state = *static_cast<State *>(argument);
    locale_t previous = LIBC_NAMESPACE::uselocale(state.locale);
    while (!state.start->load()) {
    }
    regmatch_t matches[3];
    state.passed = true;
    for (unsigned i = 0; i != 8; ++i) {
      state.passed &= LIBC_NAMESPACE::regexec(state.re, "\xc3\xa9\xc3\xa9!", 3,
                                              matches, 0) == 0 &&
                      matches[0].rm_so == 0 && matches[0].rm_eo == 5 &&
                      matches[1].rm_so == 0 && matches[1].rm_eo == 2 &&
                      matches[2].rm_so == -1 && matches[2].rm_eo == -1;
      state.passed &=
          LIBC_NAMESPACE::regexec(state.re, "a!", 0, nullptr, 0) == REG_NOMATCH;
    }
    LIBC_NAMESPACE::uselocale(previous);
    return nullptr;
  };
  pthread_t threads[4];
  unsigned created = 0;
  for (; created != 4; ++created) {
    states[created] = {locale, &re, &start};
    int error = LIBC_NAMESPACE::pthread_create(&threads[created], nullptr,
                                               worker, &states[created]);
    EXPECT_EQ(error, 0);
    if (error)
      break;
  }
  start.store(true);
  for (unsigned i = 0; i != created; ++i) {
    ASSERT_EQ(LIBC_NAMESPACE::pthread_join(threads[i], nullptr), 0);
    EXPECT_TRUE(states[i].passed);
  }
  LIBC_NAMESPACE::regfree(&re);
  LIBC_NAMESPACE::uselocale(previous);
  LIBC_NAMESPACE::freelocale(locale);
}
