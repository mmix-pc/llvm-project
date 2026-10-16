//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <exception>

extern "C" void record_destroy();
namespace {
class Payload : public std::exception {
public:
  const char* what() const noexcept override { return "retained exception"; }
  ~Payload() override { record_destroy(); }
};
} // namespace
extern "C" void throw_payload() { throw Payload{}; }
extern "C" void payload_terminate() { __builtin_trap(); }
