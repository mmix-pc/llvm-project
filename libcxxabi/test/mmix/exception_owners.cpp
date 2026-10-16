//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../../src/mmix/ExceptionStorage.h"
#include "cxxabi.h"
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <pthread.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
struct Ticket {
  bool live;
  void* images[2];
};
Ticket tickets[32]{};
unsigned live, acquires, destroyed, fail_at;
const char* payload_path;

void* hold(uintptr_t pc) {
  Dl_info info{}, self{};
  assert(dladdr(reinterpret_cast<void*>(pc), &info));
  assert(dladdr(reinterpret_cast<void*>(&hold), &self));
  if (info.dli_fbase == self.dli_fbase)
    return nullptr; // The executable and its statically linked runtime stay.
  void* handle = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
  assert(handle);
  return handle;
}
void verify(bool retained) {
  void* handle = dlopen(payload_path, RTLD_NOW | RTLD_NOLOAD);
  assert(bool(handle) == retained);
  if (handle)
    assert(!dlclose(handle));
}
} // namespace

extern "C" long __llvm_libc_mmix_retain_exception_images(uintptr_t type, uintptr_t callback, uint64_t* out) {
  assert(out && !*out && type && callback);
  if (++acquires == fail_at)
    return 12;
  for (unsigned i = 0; i != 32; ++i) {
    if (tickets[i].live)
      continue;
    tickets[i] = {true, {hold(type), hold(callback)}};
    *out       = i + 1;
    ++live;
    return 0;
  }
  assert(false);
  return 12;
}
extern "C" long __llvm_libc_mmix_release_exception_images(uint64_t token) {
  assert(token && token <= 32 && tickets[token - 1].live);
  auto& ticket = tickets[token - 1];
  for (void* image : ticket.images)
    if (image)
      assert(!dlclose(image));
  ticket = {};
  --live;
  return 0;
}
extern "C" void record_destroy() {
  assert(live == 2);
  verify(true);
  ++destroyed;
}

int main(int argc, char** argv) {
  assert(argc == 2);
  payload_path = argv[1];
  using namespace __cxxabiv1::mmix;
  assert(!allocate_exception_storage(SIZE_MAX));
  void* uninitialized = allocate_exception_storage(128);
  assert(uninitialized);
  free_exception_storage(uninitialized); // Throwing-object constructor failed.
  void* failed_object = __cxxabiv1::__cxa_allocate_exception(128);
  assert(reinterpret_cast<uintptr_t>(failed_object) % alignof(max_align_t) == 0);
  __cxxabiv1::__cxa_free_exception(failed_object);
  assert(!live && !acquires);

  void* handle = dlopen(payload_path, RTLD_NOW);
  assert(handle);
  auto raise = reinterpret_cast<void (*)()>(dlsym(handle, "throw_payload"));
  assert(raise);
  auto handler = reinterpret_cast<void (*)()>(dlsym(handle, "payload_terminate"));
  assert(handler);
  auto saved_handler = std::set_terminate(handler);
  std::exception_ptr first;
  try {
    raise();
  } catch (...) {
    first = std::current_exception();
  }
  std::set_terminate(saved_handler);
  assert(live == 2 && acquires == 2 && !destroyed);
  std::exception_ptr copy = first;
  assert(!dlclose(handle));
  verify(true);
  first = nullptr;
  std::thread worker([copy] {
    try {
      try {
        std::rethrow_exception(copy); // A dependent exception owns the primary.
      } catch (...) {
        throw;
      }
    } catch (const std::exception& value) {
      assert(!std::strcmp(value.what(), "retained exception"));
    }
  });
  worker.join();
  assert(live == 2 && acquires == 2 && !destroyed);
  copy = nullptr;
  assert(!live && destroyed == 1);
  verify(false);

  // Trivial primary exceptions still retain RTTI through make_exception_ptr.
  auto trivial = std::make_exception_ptr(17);
  assert(live == 2);
  trivial = nullptr;
  assert(!live);

  struct CopyThrows {
    CopyThrows() = default;
    CopyThrows(const CopyThrows &) { throw 23; }
  };
  auto failed_copy = std::make_exception_ptr(CopyThrows{});
  assert(live == 2); // The initialized but unconstructed primary was released.
  try {
    std::rethrow_exception(failed_copy);
  } catch (int value) {
    assert(value == 23);
  }
  failed_copy = nullptr;
  assert(!live);

  // Foreign pthread forced unwinding must not create primary-exception owners.
  unsigned before_forced = acquires;
  pthread_t thread;
  assert(!pthread_create(&thread, nullptr, [](void*) -> void* { pthread_exit(reinterpret_cast<void*>(17)); }, nullptr));
  void* result;
  assert(!pthread_join(thread, &result));
  assert(result == reinterpret_cast<void*>(17));
  assert(!live && acquires == before_forced);

  // Retain failures terminate, rolling back the first ticket on second failure.
  for (unsigned failure : {1u, 2u}) {
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
      acquires = 0;
      fail_at  = failure;
      std::set_terminate([] { _exit(live == 0 ? 0 : 1); });
      throw 1;
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
}
