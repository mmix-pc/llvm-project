//===--- Definitions of common thread items ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/thread.h"
#include "src/__support/macros/config.h"
#include "src/__support/threads/mutex.h"
#include "src/__support/threads/tss.h"

#include "src/__support/CPP/mutex.h" // lock_guard
#include "src/__support/fixedvector.h"
#include "src/__support/macros/attributes.h"

#ifdef LIBC_MMIX_TLS_DESTRUCTORS
#include "src/__support/threads/linux/mmix/tls_destructors.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace {

using AtExitCallback = void(void *);

struct AtExitUnit {
  AtExitCallback *callback = nullptr;
  void *obj = nullptr;
  constexpr AtExitUnit() = default;
  constexpr AtExitUnit(AtExitCallback *cb, void *o) : callback(cb), obj(o) {}
};

} // anonymous namespace

#ifndef LIBC_MMIX_TLS_DESTRUCTORS
class ThreadAtExitCallbackMgr {
  Mutex mtx;
  // TODO: Use a BlockStore when compiled for production.
  FixedVector<AtExitUnit, 1024> callback_list;

public:
  constexpr ThreadAtExitCallbackMgr()
      : mtx(/*is_priority_inherit=*/false, /*is_recursive=*/false,
            /*is_robust=*/false, /*is_pshared=*/false) {}

  int add_callback(AtExitCallback *callback, void *obj) {
    cpp::lock_guard lock(mtx);
    if (callback_list.push_back({callback, obj}))
      return 0;
    return -1;
  }

  void call() {
    mtx.lock();
    while (!callback_list.empty()) {
      auto atexit_unit = callback_list.back();
      callback_list.pop_back();
      mtx.unlock();
      atexit_unit.callback(atexit_unit.obj);
      mtx.lock();
    }
    mtx.unlock();
  }
};
#endif

static LIBC_THREAD_LOCAL ThreadAtExitCallbackMgr atexit_callback_mgr;

// The function __cxa_thread_atexit is provided by C++ runtimes like libcxxabi.
// It is used by thread local object runtime to register destructor calls. To
// actually register destructor call with the threading library, it calls
// __cxa_thread_atexit_impl, which is to be provided by the threading library.
// The semantics are very similar to the __cxa_atexit function except for the
// fact that the registered callback is thread specific.
extern "C" int __cxa_thread_atexit_impl(AtExitCallback *callback, void *obj,
                                      void *dso) {
#ifdef LIBC_MMIX_TLS_DESTRUCTORS
  return atexit_callback_mgr.add_callback(callback, obj, dso);
#else
  (void)dso;
  return atexit_callback_mgr.add_callback(callback, obj);
#endif
}

namespace internal {

ThreadAtExitCallbackMgr *get_thread_atexit_callback_mgr() {
  return &atexit_callback_mgr;
}

void call_atexit_callbacks(ThreadAttributes *attrib) {
  attrib->atexit_callback_mgr->call();
  call_tss_destructors();
}

} // namespace internal

} // namespace LIBC_NAMESPACE_DECL
