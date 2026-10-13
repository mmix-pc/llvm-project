// REQUIRES: mmix-registered-target, lld
// RUN: split-file %s %t
// RUN: %python %t/check.py %clang llvm-ar llvm-readobj %t

//--- main.c
extern long imported;
extern long member(long);
extern long native(long);
extern long optional __attribute__((weak));
long value = 17;
__attribute__((visibility("hidden"))) long hidden = 19;
__attribute__((visibility("protected"))) long protected_value = 23;
__attribute__((noinline)) long replaceable(void) { return value; }
long *address(void) { return &value; }
long *weak_address(void) { return &optional; }
long read_hidden(void) { return hidden + protected_value; }
long entry(void) { return replaceable() + member(imported) + native(1); }

//--- member.c
extern long imported;
long member(long n) { return n + imported; }
//--- unused.c
extern long missing(void);
long unused(void) { return missing(); }
//--- native.c
long native(long n) { return n + 31; }
//--- provider.c
long imported = 29;

//--- check.py
import pathlib
import subprocess
import sys

clang, ar, readobj, root = sys.argv[1:]
root = pathlib.Path(root)
base = [clang, "--target=mmix-unknown-linux", "-fPIC",
        "-fsemantic-interposition", "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables"]

def run(args):
    try:
        return subprocess.check_output(list(map(str, args)), stderr=subprocess.STDOUT,
                                       text=True)
    except subprocess.CalledProcessError as error:
        print(error.output, file=sys.stderr)
        raise

def compile(name, output, flags):
    run(base + flags + ["-c", root / (name + ".c"), "-o", output])

compile("provider", root / "provider.o", ["-O2"])
run(base + ["-shared", "-nostdlib", root / "provider.o",
            "-Wl,-soname,libprovider.so", "-o", root / "provider.so"])
compile("native", root / "native.o", ["-O2"])

for level in (0, 2):
    for lto in (False, True):
        prefix = root / ("o%d-%s" % (level, "lto" if lto else "native"))
        flags = ["-O%d" % level] + (["-flto=full"] if lto else [])
        objects = {}
        for name in ("main", "member", "unused"):
            objects[name] = str(prefix) + "-" + name + ".o"
            compile(name, objects[name], flags)
        archive = str(prefix) + ".a"
        run([ar, "rcs", archive, objects["unused"], objects["member"]])
        for mode, options in (("shared", ["-shared"]), ("pie", ["-pie"]),
                              ("exec", ["-no-pie", "-Wl,-Bdynamic"])):
            output = str(prefix) + "-" + mode
            run(base + flags + options + ["-nostdlib", "-e", "entry",
                "-Wl,--export-dynamic,--threads=1,-z,defs",
                objects["main"], archive, root / "native.o",
                root / "provider.so", "-o", output])
            headers = run([readobj, "-h", "--dynamic-table", output])
            assert ("Type: Executable" if mode == "exec" else
                    "Type: SharedObject") in headers, headers
            assert "libprovider.so" in headers and "BIND_NOW" in headers
            symbols = run([readobj, "--dyn-symbols", output])
            assert "Name: unused" not in symbols and "Name: missing" not in symbols
            assert "Name: hidden " not in symbols
            for name in ("entry", "value", "replaceable", "member", "native",
                         "protected_value"):
                assert "Name: " + name + " " in symbols, (output, name, symbols)
            relocs = run([readobj, "-r", output])
            for name in ("imported", "optional"):
                assert "R_MMIX_GLOB_DAT " + name + " " in relocs, (output, relocs)
            if mode == "shared":
                # Definitions remain interposable through optimization/LTO.
                for name in ("value", "replaceable", "member", "native"):
                    assert "R_MMIX_GLOB_DAT " + name + " " in relocs, (output, relocs)
            else:
                assert "R_MMIX_GLOB_DAT value " not in relocs, (output, relocs)
            assert "R_MMIX_GLOB_DAT hidden " not in relocs
            assert "R_MMIX_GLOB_DAT protected_value " not in relocs

# Archive-only bitcode must also receive PIC backend selection for ET_EXEC.
run([ar, "rcs", root / "main.a", root / "o2-lto-main.o"])
run(base + ["-nostdlib", "-no-pie", "-Wl,-Bdynamic,--threads=1",
    "-e", "entry", root / "main.a", root / "o2-lto.a", root / "native.o",
    root / "provider.so", "-o", root / "archive-exec"])
assert "R_MMIX_GLOB_DAT imported " in run([readobj, "-r", root / "archive-exec"])
