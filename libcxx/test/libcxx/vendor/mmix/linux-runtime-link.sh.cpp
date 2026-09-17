//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// REQUIRES: target=mmix-unknown-linux{{.*}}
// UNSUPPORTED: c++03, c++11, c++14, no-threads
// This is a link-only Linux regression, not an execution test. Runtime library
// exceptions/RTTI remain enabled independently of the consumer's switches.
// RUN: %{cxx} %s %{flags} %{compile_flags} -std=c++17 -O0 -frtti -fexceptions %{link_flags} -Wl,--no-gc-sections -o %t.o0.exe
// RUN: %{cxx} %s %{flags} %{compile_flags} -std=c++17 -O2 -frtti -fexceptions %{link_flags} -Wl,--no-gc-sections -o %t.o2.exe
// RUN: %{cxx} %s %{flags} %{compile_flags} -std=c++17 -O0 -fno-rtti -fno-exceptions %{link_flags} -Wl,--no-gc-sections -o %t.plain.o0.exe
// RUN: %{cxx} %s %{flags} %{compile_flags} -std=c++17 -O2 -fno-rtti -fno-exceptions %{link_flags} -Wl,--no-gc-sections -o %t.plain.o2.exe

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <typeindex>
#include <unordered_map>
#include <vector>

volatile int destroyed;
struct Base {
  virtual ~Base() { ++destroyed; }
};
struct alignas(64) Derived : Base {
  std::string text;
  explicit Derived(const std::string& value) : text(value) {}
};

int main(int argc, char**) {
  std::vector<std::string> values(3, std::string(80, 'x'));
  values.push_back(std::to_string(argc));
  std::map<int, std::string> ordered;
  ordered.emplace(argc, values.back());
  std::unordered_map<int, std::string> hashed;
  hashed.emplace(argc, ordered.at(argc));
  std::function<int(int)> apply = [&hashed](int key) { return int(hashed.at(key).size()); };
  auto shared = std::make_shared<std::string>(values.front());
  std::unique_ptr<Base> base(argc > 1 ? new Derived(*shared) : new Base);
  std::unique_ptr<Derived> optional(new (std::nothrow) Derived(*shared));
  auto now = std::chrono::system_clock::now();
  auto steady = std::chrono::steady_clock::now();
  std::mutex mutex;
  std::condition_variable condition;
  std::unique_lock<std::mutex> lock(mutex);
  condition.wait_until(lock, now);
  condition.notify_all();
  std::once_flag once;
  std::call_once(once, [&] { values.push_back(*shared); });
  std::recursive_mutex recursive;
  recursive.lock();
  recursive.unlock();
  std::shared_mutex readers;
  readers.lock_shared();
  readers.unlock_shared();
  std::error_code error(argc, std::generic_category());
  values.push_back(error.message());
#if defined(__cpp_rtti)
  values.push_back(typeid(*base).name());
  values.push_back(std::to_string(std::type_index(typeid(*base)).hash_code()));
  if (auto* derived = dynamic_cast<Derived*>(base.get()))
    values.push_back(derived->text);
#endif
#if defined(__cpp_exceptions)
  try {
    Derived cleanup(*shared);
    throw std::system_error(error, values.back());
  } catch (const std::exception& caught) {
    values.push_back(caught.what());
  }
#endif
  return apply(argc) + int(values.size()) + int(bool(optional)) +
         int(steady.time_since_epoch().count() == 0);
}
