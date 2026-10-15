// REQUIRES: mmix-registered-target
// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: %python %t/check.py %clang %t

//--- check.py
import pathlib
import subprocess
import sys

clang, directory = sys.argv[1:]
root = pathlib.Path(directory) / "sysroot"
resource = pathlib.Path(directory) / "resource"
lib = root / "usr/lib"
lib.mkdir(parents=True)
for name in ("libc++.so", "libc++abi.so", "libunwind.so", "libc.so",
             "libc++abi.so.1", "libunwind.so.1", "Scrt1.o", "crt1.o",
             "libc++.a", "libc++abi.a", "libunwind.a", "libc.a", "libm.a"):
    (lib / name).touch()
(lib / "mmix-shared-libc-profile").write_text("mmix-linux-shared-pthread-c\n")
profile = lib / "mmix-shared-cxx-profile"
profile.write_text("mmix-linux-shared-pthread-cxx\n")
(root / "lib").mkdir()
(root / "lib/ld-llvm-mmix.so.1").touch()
(root / "usr/include/c++/v1").mkdir(parents=True)
(root / "usr/include/c++/v1/__config_site").touch()
(root / "usr/include/pthread.h").touch()
(resource / "include").mkdir(parents=True)
rt = resource / "lib/mmix-unknown-linux"
rt.mkdir(parents=True)
for name in ("clang_rt.crtbegin.o", "clang_rt.crtend.o", "libclang_rt.builtins.a"):
    (rt / name).touch()
obj = root / "input.o"
obj.touch()
base = [clang, "--no-default-config", "--driver-mode=g++", "--target=mmix-unknown-linux",
        "--sysroot=" + str(root), "-resource-dir", str(resource), "-###", str(obj)]
def check(args, error=False):
    result = subprocess.run(base + args, capture_output=True, text=True)
    assert (result.returncode != 0) == error, result.stderr
    return result.stderr
for mode in (["-pie"], ["-shared"], ["-no-pie", "-Wl,-Bdynamic"]):
    for pthread in ([], ["-pthread"]):
        text = check(mode + pthread)
        order = [str(rt / "clang_rt.crtbegin.o"), str(obj), str(lib / "libc++.so"),
                 str(lib / "libc++abi.so"), str(lib / "libunwind.so"),
                 str(rt / "clang_rt.crtend.o")]
        assert [text.index(x) for x in order] == sorted(text.index(x) for x in order)
        for name in ("libc++.a", "libc++abi.a", "libunwind.a", "libc.a", "libm.a"):
            assert str(lib / name) not in text
        assert (str(lib / "Scrt1.o") in text) == (mode != ["-shared"])
    text = check(mode + ["-nostartfiles"])
    assert "crtbegin" not in text and "crtend" not in text and "Scrt1.o" not in text
    text = check(mode + ["-nodefaultlibs"])
    assert "crtbegin" in text and "libc++.so" not in text and "builtins" not in text
    text = check(mode + ["-nostdlib"])
    assert "crtbegin" not in text and "libc++.so" not in text
    text = check(mode + ["-flto=full", "-O2"])
    assert "-relocation-model=pic" in text and "-plugin-opt=O2" in text
text = check([])
assert str(lib / "libc++.a") in text and str(lib / "libc++.so") not in text
for path in (lib / "libc++.so", lib / "libc++abi.so", lib / "libunwind.so",
             rt / "clang_rt.crtbegin.o", rt / "clang_rt.crtend.o"):
    path.unlink()
    assert str(path) in check(["-pie", "-L" + str(root)], error=True)
    path.touch()
profile.write_text("invalid\n")
assert "incompatible" in check(["-pie"], error=True)
profile.unlink()
assert "mmix-shared-cxx-profile" in check(["-shared"], error=True)
check(["-shared", "-nostdlib"])
