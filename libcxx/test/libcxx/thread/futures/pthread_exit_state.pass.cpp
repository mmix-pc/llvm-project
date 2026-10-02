//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03, no-threads, no-exceptions
// REQUIRES: libcpp-has-thread-api-pthread

#include <atomic>
#include <cassert>
#include <future>
#include <pthread.h>

struct Context {
  std::promise<int> value;
  std::promise<int&> reference;
  std::promise<void> empty;
  std::atomic<bool> destroyed{false};
  int number = 42;
  int scenario;
};

struct Guard {
  Context* context;
  ~Guard() { context->destroyed = true; }
};

void* worker(void* pointer) {
  Context& context = *static_cast<Context*>(pointer);
  thread_local Guard guard{&context};
  (void)&guard;
  // Each new pthread reaches its first exit-state registration here.
  switch (context.scenario) {
  case 0: context.value.set_value_at_thread_exit(context.number); break;
  case 1: context.reference.set_value_at_thread_exit(context.number); break;
  case 2: context.empty.set_value_at_thread_exit(); break;
  case 3: context.value.set_exception_at_thread_exit(std::make_exception_ptr(7)); break;
  case 4: context.reference.set_exception_at_thread_exit(std::make_exception_ptr(7)); break;
  case 5: context.empty.set_exception_at_thread_exit(std::make_exception_ptr(7)); break;
  }
  return nullptr;
}

int main(int, char**) {
  for (int scenario = 0; scenario != 6; ++scenario) {
    Context context;
    context.scenario = scenario;
    auto value = context.value.get_future();
    auto reference = context.reference.get_future();
    auto empty = context.empty.get_future();
    pthread_t thread;
    assert(pthread_create(&thread, nullptr, worker, &context) == 0);
    bool caught = false;
    try {
      switch (scenario % 3) {
      case 0: assert(value.get() == 42); break;
      case 1: assert(&reference.get() == &context.number); break;
      case 2: empty.get(); break;
      }
    } catch (int error) {
      assert(error == 7);
      caught = true;
    }
    assert(caught == (scenario >= 3));
    assert(context.destroyed);
    assert(pthread_join(thread, nullptr) == 0);
  }
  return 0;
}
