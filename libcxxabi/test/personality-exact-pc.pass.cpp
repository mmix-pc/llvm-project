//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Exercise the real personality with controlled DWARF LSDA and context access.
// Linker wrapping leaves the unwinder's own definitions intact.
// REQUIRES: linux, static-libcxxabi
// UNSUPPORTED: no-exceptions, c++03, target={{arm.*}}, target={{ve-.*}}
// ADDITIONAL_LINK_FLAGS: -Wl,--wrap=_Unwind_GetIP,--wrap=_Unwind_GetIPInfo,--wrap=_Unwind_GetRegionStart,--wrap=_Unwind_GetLanguageSpecificData,--wrap=_Unwind_SetIP,--wrap=_Unwind_SetGR

#include <cassert>
#include <cstdint>
#include <unwind.h>

extern "C" _Unwind_Reason_Code __gxx_personality_v0(
    int, _Unwind_Action, uint64_t, _Unwind_Exception*, _Unwind_Context*);

static const uintptr_t base = 0x1000;
static uintptr_t pc, landingPad;
static int beforeInstruction, infoCalls, legacyCalls, registerWrites;

// Omitted LPStart/type table, ULEB128 call-site entries. Adjacent intervals
// [0,4) and [4,8) have distinct cleanups; [8,9) has no landing pad.
static const unsigned char lsda[] = {
    0xff, 0xff, 0x01, 12, 0, 4, 16, 0, 4, 4, 32, 0, 8, 1, 0, 0};

extern "C" uintptr_t __wrap__Unwind_GetIP(_Unwind_Context*) {
  ++legacyCalls;
  return pc;
}

extern "C" uintptr_t __wrap__Unwind_GetIPInfo(_Unwind_Context*, int* before) {
  ++infoCalls;
  *before = beforeInstruction;
  return pc;
}

extern "C" uintptr_t __wrap__Unwind_GetRegionStart(_Unwind_Context*) { return base; }
extern "C" uintptr_t __wrap__Unwind_GetLanguageSpecificData(_Unwind_Context*) {
  return reinterpret_cast<uintptr_t>(lsda);
}
extern "C" void __wrap__Unwind_SetIP(_Unwind_Context*, uintptr_t ip) { landingPad = ip; }
extern "C" void __wrap__Unwind_SetGR(_Unwind_Context*, int, uintptr_t) { ++registerWrites; }

static void check(unsigned offset, int before, uintptr_t expected) {
  _Unwind_Exception exception = {};
  // The mock accessors never dereference this opaque context.
  auto* context = reinterpret_cast<_Unwind_Context*>(&exception);
  pc = base + offset;
  beforeInstruction = before;
  infoCalls = legacyCalls = registerWrites = 0;
  landingPad = 0;
  _Unwind_Reason_Code result = __gxx_personality_v0(
      1, static_cast<_Unwind_Action>(_UA_CLEANUP_PHASE | _UA_FORCE_UNWIND),
      0, &exception, context);
  assert(result == (expected ? _URC_INSTALL_CONTEXT : _URC_CONTINUE_UNWIND));
  assert(landingPad == expected);
  assert(infoCalls == 1 && legacyCalls == 0);
  assert(registerWrites == (expected ? 2 : 0));
}

int main(int, char**) {
  check(0, 1, base + 16);
  check(1, 0, base + 16);
  check(3, 1, base + 16);
  check(4, 0, base + 16);
  check(4, 1, base + 32);
  check(7, 1, base + 32);
  check(8, 0, base + 32);
  check(8, 1, 0);
  return 0;
}
