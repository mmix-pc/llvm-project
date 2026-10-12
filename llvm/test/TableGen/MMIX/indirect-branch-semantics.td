// RUN: llvm-tblgen -I %p -I %p/../../../include \
// RUN:   -I %p/../../../lib/Target/MMIX %s

include "MMIX.td"

assert !and(PseudoIndirectBranch.isBranch,
            PseudoIndirectBranch.isIndirectBranch,
            PseudoIndirectBranch.isTerminator,
            PseudoIndirectBranch.isBarrier),
       "computed goto must terminate the block without fallthrough";
assert !not(!or(PseudoIndirectBranch.isCall, PseudoIndirectBranch.isReturn)),
       "computed goto must not use call or return frame handling";
assert !and(!eq(!size(PseudoIndirectBranch.Defs), 1),
            !eq(!head(PseudoIndirectBranch.Defs), R255)),
       "computed goto may only clobber the reserved link-result register";
assert !eq(PseudoIndirectBranch.Size, 4),
       "computed goto must lower to one GO instruction";
