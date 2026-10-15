// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: %python %t/check.py %clang %t

// Empty files test Driver ownership and selection, not runtime execution.
//--- check.py
import pathlib
import subprocess
import sys

clang, directory = sys.argv[1:]
work = pathlib.Path(directory)
root, resource = work / "root", work / "resource"
files = ["usr/lib/Scrt1.o", "usr/lib/libc.so", "usr/lib/libunwind.so.1",
         "usr/lib/libc++abi.so.1", "lib/ld-llvm-mmix.so.1", "usr/include/pthread.h",
         "usr/lib/crt1.o", "usr/lib/libc.a"]
for name in files:
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.touch()
profile = root / "usr/lib/mmix-shared-libc-profile"
profile.write_text("mmix-linux-shared-pthread-c\n")
(root / "usr/lib/mmix-libc-profile").write_text("mmix-linux-static-pthread-c-v1\n")
(resource / "include").mkdir(parents=True, exist_ok=True)
runtime = resource / "lib/mmix-unknown-linux"
runtime.mkdir(parents=True, exist_ok=True)
for name in ("libclang_rt.builtins.a", "clang_rt.crtbegin.o", "clang_rt.crtend.o"):
    (runtime / name).touch()
obj = work / "input.o"
obj.touch()
base = [clang, "--no-default-config", "--target=mmix-unknown-linux",
        "--sysroot=" + str(root), "-resource-dir", str(resource), "-###", str(obj)]

def check(args, yes=(), no=(), error=False):
    result = subprocess.run([*base, *args], text=True, capture_output=True)
    text = result.stderr
    assert (result.returncode != 0) == error, text
    for word in yes:
        assert word in text, (word, text)
    for word in no:
        assert word not in text, (word, text)

shared = [str(root / ("usr/lib/" + name))
          for name in ("libc.so", "libunwind.so.1", "libc++abi.so.1")]
crt = str(root / "usr/lib/Scrt1.o")
static = [str(root / "usr/lib/crt1.o"), str(root / "usr/lib/libc.a")]
for mode in (["-pie"], ["-no-pie", "-Wl,-Bdynamic"]):
    for pthread in ([], ["-pthread"]):
        check([*mode, *pthread], [crt, *shared, "--dynamic-linker=/lib/ld-llvm-mmix.so.1",
              "--no-allow-shlib-undefined", "--exclude-libs=libclang_rt.builtins.a"],
              [*static, "crtbegin", "crtend", "-lpthread", "libc++abi.a", "libunwind.a"])
    check([*mode, "-nostdlib"], no=[crt, *shared, "builtins"])
    check([*mode, "-nostartfiles"], shared, [crt, "crtbegin", "crtend"])
    check([*mode, "-nodefaultlibs"], [crt], [*shared, "builtins"])
    check([*mode, "-nolibc"], [crt, "builtins"], shared)
    check([*mode, "-flto=full", "-O2"], ["-plugin-opt=O2", "-mllvm", "-relocation-model=pic"])
    check([*mode, "--driver-mode=g++"], ["mmix-shared-cxx-profile"], error=True)
    check([*mode, "--unwindlib=libunwind"], ["mmix-shared-cxx-profile"], error=True)
check(["-shared"], [*shared, '"-shared"', '"--no-dynamic-linker"'], [crt, "crtbegin", "crtend"])
check(["-shared", "-nodefaultlibs"], no=[crt, *shared, "builtins"])
for args in ([], ["-static"], ["-no-pie"], ["-pie", "-no-pie"], ["-pthread", "-static"]):
    check(args, [*static, '"-static"'], [crt, *shared])
check(["-no-pie", "-pie"], [crt, '"-pie"'])
check(["-pie", "-static"], ["conflicting MMIX Linux link modes"], error=True)
check(["-static-pie"], ["unsupported option"], error=True)
for option in ("-fno-exceptions", "-fno-unwind-tables", "--unwindlib=none"):
    check(["-pie", "-pthread", option], ["unsupported option"], error=True)

# A private -L/-B directory may not supply a missing default provider.
for name in files[:5]:
    path = root / name
    path.unlink()
    (work / path.name).touch()
    check(["-pie", "-L" + str(work), "-B" + str(work)], [str(path), "no such file"], error=True)
    path.touch()
# Compile-only shared pthread jobs need declarations and cleanup generation,
# not the program-entry CRT or a retained static archive.
source = work / "input.c"
source.write_text("int value;\n")
for name in ("usr/lib/crt1.o", "usr/lib/libc.a", "usr/lib/Scrt1.o"):
    (root / name).unlink()
result = subprocess.run([*base[:-1], "-pie", "-pthread", "-c", str(source)],
                        text=True, capture_output=True)
assert result.returncode == 0, result.stderr
assert '"-fexceptions"' in result.stderr and '"-funwind-tables=1"' in result.stderr
profile.write_text("wrong-profile\n")
check(["-pie"], ["unrecognized MMIX Linux shared C libc profile"], error=True)
profile.unlink()
check(["-pie"], [str(profile)], error=True)
check(["-pie", "-nostdlib"], no=[crt, *shared])
check(["-shared", "-nostartfiles", "-nodefaultlibs"], no=[crt, *shared])
