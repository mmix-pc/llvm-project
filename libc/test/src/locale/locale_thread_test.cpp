//===-- Per-thread locale selection tests ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/locale_macros.h"
#include "src/locale/freelocale.h"
#include "src/locale/mb_cur_max.h"
#include "src/locale/newlocale.h"
#include "src/locale/setlocale.h"
#include "src/locale/uselocale.h"
#include "src/pthread/pthread_create.h"
#include "src/pthread/pthread_join.h"
#include "src/wchar/mbrtowc.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcLocaleThreadTest, SelectionAndImplicitStateAreThreadLocal) {
  using namespace LIBC_NAMESPACE;
  // Save category names through the implementation's restorable query form.
  char saved_name[192];
  const char *name = LIBC_NAMESPACE::setlocale(LC_ALL, nullptr);
  unsigned i = 0;
  do {
    saved_name[i] = name[i];
  } while (name[i++]);
  ASSERT_NE(LIBC_NAMESPACE::setlocale(LC_ALL, "C"), nullptr);
  locale_t utf8 = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C.UTF-8", nullptr);
  ASSERT_NE(utf8, nullptr);
  locale_t previous = LIBC_NAMESPACE::uselocale(utf8);
  wchar_t wc;
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&wc, "\xc3", 1, nullptr), size_t(-2));
  struct State {
    locale_t utf8;
    bool passed;
  } state{utf8, false};
  auto worker = [](void *arg) -> void * {
    auto &worker_state = *static_cast<State *>(arg);
    worker_state.passed =
        LIBC_NAMESPACE::uselocale(nullptr) == LC_GLOBAL_LOCALE &&
        LIBC_NAMESPACE::__ctype_get_mb_cur_max() == 1;
    worker_state.passed &=
        LIBC_NAMESPACE::uselocale(worker_state.utf8) == LC_GLOBAL_LOCALE;
    wchar_t worker_wc;
    worker_state.passed &=
        LIBC_NAMESPACE::mbrtowc(&worker_wc, "\xc3\xa9", 2, nullptr) == 2 &&
        worker_wc == 0xe9;
    worker_state.passed &=
        LIBC_NAMESPACE::uselocale(LC_GLOBAL_LOCALE) == worker_state.utf8;
    return nullptr;
  };
  pthread_t thread;
  ASSERT_EQ(LIBC_NAMESPACE::pthread_create(&thread, nullptr, worker, &state),
            0);
  ASSERT_EQ(LIBC_NAMESPACE::pthread_join(thread, nullptr), 0);
  EXPECT_TRUE(state.passed);
  EXPECT_EQ(LIBC_NAMESPACE::uselocale(nullptr), utf8);
  EXPECT_EQ(LIBC_NAMESPACE::mbrtowc(&wc, "\xa9", 1, nullptr), size_t(1));
  EXPECT_EQ(wc, wchar_t(0xe9));
  LIBC_NAMESPACE::uselocale(previous);
  LIBC_NAMESPACE::freelocale(utf8);
  LIBC_NAMESPACE::setlocale(LC_ALL, saved_name);
}
