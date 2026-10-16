# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/provider.s -o %t/provider.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/consumer.s -o %t/consumer.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/empty.s -o %t/empty.o
# RUN: ld.lld -m elf64mmix_linux -shared -soname libprovider.so %t/provider.o -o %t/provider.so
# RUN: ld.lld -m elf64mmix_linux -shared -soname libunused.so %t/empty.o -o %t/unused.so
# RUN: ld.lld -m elf64mmix_linux -shared -z defs --as-needed %t/consumer.o %t/provider.so %t/unused.so -o %t/consumer.so
# RUN: llvm-readobj --dynamic-table --dyn-symbols --relocations %t/consumer.so | FileCheck %s --check-prefix=NEEDED
# RUN: ld.lld -m elf64mmix_linux -shared -z defs --no-as-needed %t/consumer.o %t/provider.so %t/unused.so -o %t/all.so
# RUN: llvm-readobj --needed-libs %t/all.so | FileCheck %s --check-prefix=ALL
# RUN: not ld.lld -m elf64mmix_linux -shared -z defs %t/consumer.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNDEFINED
# RUN: not ld.lld -shared %t/consumer.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=GENERIC
# RUN: not ld.lld -m elf64mmix_linux %t/consumer.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=STATIC
# RUN: %python %t/mutate.py %t/provider.so %t
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/plt.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=TAG
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/rel.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=TAG
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/relr.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=TAG
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/tlsdesc.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=TAG
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/audit.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=TAG
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/unknown.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=TAG
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/flags1.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAGS1
# RUN: ld.lld -m elf64mmix_linux -shared %t/empty.o %t/checksum.so -o %t/checksum-consumer.so
# RUN: ld.lld -m elf64mmix_linux -shared -z defs %t/consumer.o %t/linux.so -o %t/linux-consumer.so
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/abi.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=ABI
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/osabi.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=ABI
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/flags.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=ABI
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/machine.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=MACHINE
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/ifunc.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=IFUNC
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/unique.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNIQUE
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/semantic.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=SEMANTICS
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/version.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=TAG
# RUN: not ld.lld -m elf64mmix_linux -shared %t/empty.o %t/no-null.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=NULL
# NEEDED: NEEDED {{.*}}libprovider.so
# NEEDED-NOT: libunused.so
# NEEDED: R_MMIX_64 protected 0x0
# NEEDED: R_MMIX_64 function 0x0
# NEEDED: R_MMIX_64 weak 0x0
# NEEDED: R_MMIX_64 absent 0x0
# NEEDED: Name: protected
# NEEDED: Section: Undefined
# NEEDED: Name: function
# NEEDED: Section: Undefined
# ALL-DAG: libprovider.so
# ALL-DAG: libunused.so
# UNDEFINED: undefined symbol: protected
# GENERIC: MMIX does not support shared object output
# STATIC: MMIX Linux dynamic executables require an absolute interpreter path without parent traversal
# ABI: unsupported MMIX Linux shared object ABI
# MACHINE: incompatible with elf64mmix_linux
# IFUNC: MMIX Linux does not support IFUNC symbols
# UNIQUE: MMIX Linux does not support GNU_UNIQUE symbols
# SEMANTICS: unsupported MMIX Linux DT_FLAGS
# TAG: unsupported MMIX Linux dynamic tag
# FLAGS1: unsupported MMIX Linux DT_FLAGS_1
# NULL: dynamic sections must be DT_NULL terminated

#--- provider.s
.text
.globl function
.type function,@function
function: POP 0,0
.data
.p2align 3
.globl protected
.protected protected
.type protected,@object
protected: .quad 1
.weak weak
.type weak,@object
weak: .quad 2
#--- consumer.s
.data
.p2align 3
.quad protected
.quad function
.quad weak
.weak absent
.quad absent
#--- empty.s
.text
SWYM 0,0,0
#--- mutate.py
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
out = pathlib.Path(sys.argv[2])
def change(name, offset, fmt, value):
    b = bytearray(data)
    struct.pack_into(fmt, b, offset, value)
    (out / (name + ".so")).write_bytes(b)

change("abi", 8, "B", 1)
change("osabi", 7, "B", 9)
change("linux", 7, "B", 3)
change("flags", 48, ">I", 1)
change("machine", 18, ">H", 62)
shoff = struct.unpack_from(">Q", data, 40)[0]
shnum = struct.unpack_from(">H", data, 60)[0]
for i in range(shnum):
    _, typ, _, _, off, size, *_ = struct.unpack_from(">II4QII2Q", data, shoff + i * 64)
    if typ == 11:
        info = data[off + 24 + 4]
        change("ifunc", off + 24 + 4, "B", (info & 0xf0) | 10)
        change("unique", off + 24 + 4, "B", (10 << 4) | (info & 15))
    if typ == 6:
        for p in range(off, off + size, 16):
            tag, value = struct.unpack_from(">qQ", data, p)
            if tag == 0:
                change("no-null", p, ">q", 24)
            if tag == 30:
                for name, replacement in (("plt", 20), ("rel", 17),
                    ("relr", 36), ("tlsdesc", 0x6ffffef6),
                    ("audit", 0x6ffffefc), ("unknown", 0x60001234),
                    ("checksum", 0x6ffffdf8)):
                    change(name, p, ">q", replacement)
            if tag == 0x6ffffffb:
                change("flags1", p + 8, ">Q", value | 0x80)
            if tag == 30:
                change("semantic", p + 8, ">Q", value | 4)
                # DT_VERNEEDNUM advertises unsupported symbol-version semantics.
                change("version", p, ">q", 0x6fffffff)
