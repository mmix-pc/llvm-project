//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../src/DwarfParser.hpp"

struct AddressTypes {
  typedef uintptr_t pint_t;
  typedef intptr_t sint_t;
};

int main(int, char **) {
  typedef libunwind::CFI_Parser<AddressTypes> Parser;
  typedef Parser::PrologInfo State;
  State state;
  State initial(State::InitializeTime::kLazy);
  for (unsigned reg = 0; reg <= Parser::kMaxRegisterNumber; ++reg) {
    auto zero = state.getRegisterLocation(reg);
    assert(zero.location == Parser::kRegisterUnused);
    assert(!zero.initialStateSaved && zero.value == 0);
    state.setRegister(reg, Parser::kRegisterAtExpression, INT64_MIN + reg,
                      initial);
    state.setRegisterLocation(reg, Parser::kRegisterInCFA, initial);
    state.setRegisterValue(reg, INT64_MAX - reg, initial);
    auto value = state.getRegisterLocation(reg);
    assert(value.location == Parser::kRegisterInCFA);
    assert(value.initialStateSaved && value.value == INT64_MAX - reg);
  }
  State snapshot = state;
  for (unsigned reg = 0; reg <= Parser::kMaxRegisterNumber; ++reg) {
    state.restoreRegisterToInitialState(reg, initial);
    auto value = state.getRegisterLocation(reg);
    assert(value.location == Parser::kRegisterUnused && value.value == 0);
    assert(!value.initialStateSaved);
  }
  state = snapshot;
  for (unsigned reg = 0; reg <= Parser::kMaxRegisterNumber; ++reg) {
    auto value = state.getRegisterLocation(reg);
    assert(value.location == Parser::kRegisterInCFA);
    assert(value.initialStateSaved && value.value == INT64_MAX - reg);
    state.restoreRegisterToInitialState(reg, initial);
    assert(state.getRegisterLocation(reg).location == Parser::kRegisterUnused);
  }
  return 0;
}
