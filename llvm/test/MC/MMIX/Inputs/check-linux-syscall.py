"""Check the emitted adapter's data permutation, not Linux execution."""

from pathlib import Path
import sys

code = Path(sys.argv[1]).read_bytes()
assert len(code) == 40
initial = [object() for _ in range(256)]
registers = initial.copy()
for offset in range(0, 32, 4):
    opcode, dest, src, immediate = code[offset : offset + 4]
    assert opcode == 0xC1 and immediate == 0, "expected full-word OR copy"
    registers[dest] = registers[src]
assert registers[231:237] == initial[232:238], "sixth argument was lost"
assert registers[237] is initial[231], "syscall number was lost"
assert code[32:36] == bytes([0, 1, 0, 0]), "expected Linux TRAP"
for reg in range(256):
    if reg not in {*range(231, 238), 255}:
        assert registers[reg] is initial[reg], "callee-preserved state changed"

# The ordinary-return contract allows exactly these kernel register clobbers.
# POP 0,0 restores the caller's local window without transferring local results;
# the raw result remains in global r231. No GET/PUT or software-stack access is
# present, so the helper cannot overwrite rJ, SP, TP or the frame pointer.
result = object()
registers[231], registers[237], registers[255] = result, object(), object()
assert code[36:] == bytes([0xF8, 0, 0, 0])
assert registers[231] is result
