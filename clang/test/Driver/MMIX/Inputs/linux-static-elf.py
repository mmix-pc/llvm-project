import json
from pathlib import Path
import subprocess
import sys


root = Path(sys.argv[1])
ar, readobj = sys.argv[2:4]
clang = sys.argv[4:]


def run(args):
    print("RUN:", " ".join(map(str, args)), flush=True)
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    if result.returncode:
        print(result.stdout, flush=True)
        result.check_returncode()
    return result.stdout


def inspect(path):
    return json.loads(run([readobj, "--elf-output-style=JSON", "--file-headers",
                           "--sections", "--section-data", "--symbols",
                           "--relocations", path]))[0]


def sections(obj):
    return {s["Section"]["Index"]: s["Section"] for s in obj["Sections"]}


def symbols(obj):
    return {s["Symbol"]["Name"]["Name"]: s["Symbol"] for s in obj["Symbols"]}


def check_identity(obj, executable):
    header = obj["ElfHeader"]
    assert header["Ident"]["Class"]["Value"] == 2
    assert header["Ident"]["DataEncoding"]["Value"] == 2
    assert header["Machine"]["Value"] == 80
    assert header["Type"].startswith("Executable" if executable else "Relocatable")
    assert header["Flags"]["Value"] == 0
    assert not any(s["Name"]["Name"].startswith(".MMIX.reg_contents")
                   for s in sections(obj).values())
    assert "R_MMIX_BASE_PLUS_OFFSET" not in json.dumps(obj["Relocations"])


def allocated_contents(obj):
    return [(s["Name"]["Name"], s["Address"], s["Size"], s["Type"],
             s.get("SectionData")) for s in sections(obj).values()
            if s["Flags"]["Value"] & 2]


def check_link(obj, gc):
    check_identity(obj, True)
    assert not obj["Relocations"]
    syms, secs = symbols(obj), sections(obj)
    for name in ("_start", "via_cpp", "ir_use", "leaf", "values", "data_ptr",
                 "fn_ptr", "ir_ptr", "bss_words", "sink"):
        assert syms[name]["Section"]["Value"] != 0, name
    assert "unextracted" not in syms
    assert "unavailable" not in syms
    assert ("unused_selected" not in syms) == gc
    assert obj["ElfHeader"]["Entry"] == syms["_start"]["Value"]
    assert obj["ElfHeader"]["Entry"] % 4 == 0

    def value(name, offset=0):
        sym = syms[name]
        sec = secs[sym["Section"]["Value"]]
        start = sym["Value"] - sec["Address"] + offset
        data = bytes(sec["SectionData"]["Bytes"])[start:start + 8]
        assert len(data) == 8, name
        return int.from_bytes(data, "big")

    assert value("data_ptr") == syms["values"]["Value"] + 8
    assert value("fn_ptr") == syms["leaf"]["Value"]
    assert value("ir_ptr") == syms["values"]["Value"] + 16
    assert [value("values", i * 8) for i in range(3)] == [0x1122, 0x3344, 0x5566]
    bss = syms["bss_words"]
    assert bss["Size"] == 64
    assert secs[bss["Section"]["Value"]]["Type"]["Name"] == "SHT_NOBITS"
    for sym in syms.values():
        assert not (sym["Name"]["Name"] and sym["Section"]["Value"] == 0), sym


# Each variant is sequential, including the Generic isolation control.
for triple in ("mmix-unknown-linux", "mmix-unknown-linux-unknown",
               "mmix-unknown-unknown"):
    for opt in ("-O0", "-O2"):
        work = root / (triple + opt)
        work.mkdir(exist_ok=True)
        cc = clang + ["--target=" + triple, opt, "-ffreestanding", "-nostdinc",
                      "-fno-stack-protector", "-ffunction-sections", "-fdata-sections"]
        for source, output in (("entry.c", "entry.o"), ("provider.c", "provider.o"),
                               ("consumer.cpp", "cpp.o"), ("consumer.ll", "ir.o"),
                               ("unused.c", "unused.o")):
            extra = ["-fno-exceptions", "-fno-rtti"] if source.endswith(".cpp") else []
            run(cc + extra + ["-c", root / source, "-o", work / output])
            obj = inspect(work / output)
            check_identity(obj, False)
            if output in ("cpp.o", "ir.o"):
                assert "R_MMIX_GETA" in json.dumps(obj["Relocations"])
        archive = work / "libinputs.a"
        run([ar, "rcs", archive, work / "unused.o", work / "provider.o"])
        link = cc + ["-nostdlib", "-Wl,-e,_start", work / "entry.o",
                     work / "cpp.o", work / "ir.o"]
        if triple == "mmix-unknown-unknown":
            # Generic retains its explicit three-option freestanding contract.
            link += ["-nostartfiles", "-nodefaultlibs"]
        for gc in (False, True):
            output = work / ("gc" if gc else "all")
            options = ["-Wl,--gc-sections"] if gc else []
            run(link + [archive] + options + ["-o", output])
            obj = inspect(output)
            check_link(obj, gc)
            stripped = work / (output.name + "-stripped")
            run(link + [archive] + options + ["-s", "-o", stripped])
            stripped_obj = inspect(stripped)
            check_identity(stripped_obj, True)
            assert not stripped_obj["Symbols"]
            assert not stripped_obj["Relocations"]
            assert allocated_contents(stripped_obj) == allocated_contents(obj)
        # Removing the archive must expose real unresolved definitions.
        missing = subprocess.run(link + ["-o", work / "missing"], text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert missing.returncode != 0
        assert "undefined symbol: values" in missing.stdout, missing.stdout

print("MMIX runtime-free static ELF matrix passed")
