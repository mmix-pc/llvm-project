import json
from pathlib import Path
import re
import subprocess
import sys


root = Path(sys.argv[1])
ar, readobj, objdump = sys.argv[2:5]
clang = sys.argv[5:]


def run(args, error=None):
    print("RUN:", " ".join(map(str, args)), flush=True)
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    if error is not None:
        assert result.returncode != 0, result.stdout
        assert error in result.stdout, result.stdout
    elif result.returncode:
        print(result.stdout, flush=True)
        result.check_returncode()
    return result.stdout


def inspect(path):
    return json.loads(run([readobj, "--elf-output-style=JSON", "--file-headers",
                           "--program-headers", "--sections", "--section-data",
                           "--symbols", "--relocations", path]))[0]


def check_output(path, alignment):
    obj = inspect(path)
    assert obj["ElfHeader"]["Type"].startswith("Executable")
    assert obj["ElfHeader"]["Machine"]["Value"] == 80
    assert obj["ElfHeader"]["Ident"]["DataEncoding"]["Value"] == 2
    assert not obj["Relocations"]
    headers = [p["ProgramHeader"] for p in obj["ProgramHeaders"]]
    tls, = [p for p in headers if p["Type"]["Name"] == "PT_TLS"]
    assert tls["Alignment"] == alignment, tls
    assert tls["VirtualAddress"] % alignment == 0, tls
    assert tls["FileSize"] == 24, tls
    assert tls["MemSize"] == alignment + 24, tls
    assert not any(p["Type"]["Name"] in ("PT_DYNAMIC", "PT_INTERP")
                   for p in headers)
    assert any(p["Type"]["Name"] == "PT_LOAD" and
               p["Flags"]["Value"] & 4 and
               p["Offset"] <= tls["Offset"] and
               tls["Offset"] + 24 <= p["Offset"] + p["FileSize"] and
               tls["VirtualAddress"] - p["VirtualAddress"] ==
               tls["Offset"] - p["Offset"] for p in headers)
    syms = {s["Symbol"]["Name"]["Name"]: s["Symbol"] for s in obj["Symbols"]}
    secs = {s["Section"]["Index"]: s["Section"] for s in obj["Sections"]}
    assert "unused" not in syms and "missing" not in syms
    assert not any(name and s["Section"]["Value"] == 0
                   for name, s in syms.items())
    for name, offset in (("initialized", 0), ("zero", alignment)):
        sym = syms[name]
        assert sym["Type"]["Name"] == "TLS" and sym["Value"] == offset, sym
        assert sym["Size"] == 24, sym
    data = secs[syms["initialized"]["Section"]["Value"]]
    assert bytes(data["SectionData"]["Bytes"]) == b"".join(
        n.to_bytes(8, "big") for n in (11, 22, 33))
    zero = secs[syms["zero"]["Section"]["Value"]]
    assert zero["Type"]["Name"] == "SHT_NOBITS"

    # For R=0 and A>=64, the two-octa TCB gives D=A. Decode the
    # complete wyde displacement, then check TP addition and the C GEP offset.
    disasm = run([objdump, "-d", path])
    assert not re.search(r"\bGETAB?\b|__tls_get_addr|__emutls", disasm)
    for function, symbol, addend in (("_start", "initialized", 8),
                                      ("zero_address", "zero", 16)):
        body = disasm.split("<" + function + ">:", 1)[1].split("\n\n", 1)[0]
        instructions = re.findall(r"\t([A-Z]+) ([^\n]+)", body)
        start = next(i for i, (op, _) in enumerate(instructions) if op == "SETL")
        parts = instructions[start:start + 6]
        reg = parts[0][1].split(",")[0]
        value = 0
        for (op, args), expected, shift in zip(
                parts, ("SETL", "INCML", "INCMH", "INCH"), (0, 16, 32, 48)):
            assert op == expected and args.startswith(reg + ", "), parts
            value |= int(args.split(", ")[1], 16) << shift
        assert value == alignment + syms[symbol]["Value"], (function, value)
        assert parts[4] == ("ADDU", f"{reg}, r230, {reg}"), parts
        assert parts[5] == ("ADDU", f"r231, {reg}, {addend:#x}"), parts


# Both alignments use the same layout rule; 16384 exceeds the 8192-byte page.
for alignment in (64, 16384):
    for mode, options in (("o0", ["-O0"]), ("o2", ["-O2"]),
                          ("lto-o0", ["-O0", "-flto=full"]),
                          ("lto-o2", ["-O2", "-flto=full"])):
        work = root / f"{alignment}-{mode}"
        work.mkdir(exist_ok=True)
        cc = clang + ["--target=mmix-unknown-linux", "-ffreestanding",
                      "-nostdinc", f"-DALIGN={alignment}"] + options
        for source in ("consumer", "provider", "unused", "wrong"):
            run(cc + ["-c", root / (source + ".c"), "-o", work / (source + ".o")])
            magic = b"BC\xc0\xde" if mode.startswith("lto-") else b"\x7fELF"
            assert (work / (source + ".o")).read_bytes().startswith(magic)
        archive = work / "provider.a"
        run([ar, "rcs", archive, work / "unused.o", work / "provider.o"])
        link = cc + ["-nostdlib", "-Wl,-e,_start,-u,zero_address,--threads=1",
                     work / "consumer.o"]
        for provider in (work / "provider.o", archive):
            output = work / ("archive" if provider == archive else "direct")
            trace = run(link + [provider, "-Wl,--trace", "-o", output])
            assert "unused.o" not in trace, trace
            if provider == archive:
                assert "provider.a(" in trace and "provider.o)" in trace, trace
            check_output(output, alignment)
        run(link + ["-o", work / "missing"], "undefined symbol: initialized")
        # Merging a non-TLS bitcode definition invalidates threadlocal.address
        # before target code generation. Native providers reach ELF validation.
        run(link + [work / "wrong.o", "-o", work / "wrong"],
            "llvm.threadlocal.address operand isThreadLocal() must be true"
            if mode.startswith("lto-") else "requires an STT_TLS symbol")
        if mode.startswith("lto-"):
            run(cc + ["-fno-lto", "-c", root / "wrong.c", "-o",
                      work / "wrong-native.o"])
            run(link + [work / "wrong-native.o", "-o", work / "wrong-native"],
                "requires an STT_TLS symbol")

print("MMIX Linux TLS compiler-to-linker matrix passed")
