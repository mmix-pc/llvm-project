import json
import subprocess
import sys


readobj, archive = sys.argv[1:]
members = json.loads(subprocess.check_output(
    [readobj, "--elf-output-style=JSON", "--symbols", "--relocations", archive],
    text=True))
required = {
    "__multi3", "__muloti4", "__divti3", "__udivti3", "__modti3", "__umodti3",
    "__floattisf", "__floattidf", "__floatuntisf", "__floatuntidf",
    "__fixsfti", "__fixdfti", "__fixunssfti", "__fixunsdfti",
    "__udivmodti4", "__clzti2",
}
owners = {}
undefined = set()
for index, member in enumerate(members):
    assert member["FileSummary"]["Format"] == "elf64-mmix", member
    for entry in member["Symbols"]:
        symbol = entry["Symbol"]
        name = symbol["Name"]["Name"]
        if not name:
            continue
        if symbol["Section"]["Name"] == "Undefined":
            undefined.add(name)
        elif symbol["Binding"]["Name"] == "Global":
            assert name not in owners, ("duplicate provider", name)
            owners[name] = index
assert required <= owners.keys(), ("missing helpers", required - owners.keys())
assert undefined <= owners.keys(), ("unresolved helpers", undefined - owners.keys())

# Include relocations to defined symbols: an accidental self-call is not an
# undefined symbol and would survive a successful static link.
edges = {index: set() for index in range(len(members))}
for index, member in enumerate(members):
    for section in member["Relocations"]:
        for entry in section["Relocs"]:
            name = entry["Relocation"]["Symbol"]["Name"]
            if name in owners:
                edges[index].add(owners[name])

visited = set()
active = set()


def visit(index):
    assert index not in active, ("recursive helper dependency", members[index]["FileSummary"])
    if index in visited:
        return
    active.add(index)
    for dependency in edges[index]:
        visit(dependency)
    active.remove(index)
    visited.add(index)


for index in edges:
    visit(index)
