// REQUIRES: mmix-registered-target
// RUN: split-file %s %t
// RUN: %python %t/check.py %clang %t

//--- check.py
import os
from pathlib import Path
import subprocess
import sys

clang, directory = sys.argv[1:]
root = Path(directory)
explicit = root / "explicit"
explicit.mkdir(exist_ok=True)
(explicit / "explicit.h").write_text("typedef int explicit_type;\n")
source = root / "consumer.c"
source.write_text("#include <stddef.h>\n#include <explicit.h>\n"
                  "size_t builtin_size; explicit_type explicit_value;\n")
env = {key: value for key, value in os.environ.items() if key not in (
    "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH")}
resource = Path(subprocess.check_output(
    [clang, "--no-default-config", "-print-resource-dir"], text=True,
    env=env).strip()).resolve()
for target in ("mmix-unknown-unknown", "mmix-unknown-elf"):
    for language in ("c", "c++"):
        command = [clang, "--no-default-config", "--target=" + target,
                   "-x", language, "-I", str(explicit), str(source)]
        result = subprocess.run(command + ["-E", "-v"], capture_output=True,
                                text=True, env=env)
        assert result.returncode == 0, result.stderr
        paths = result.stderr.split("#include <...> search starts here:\n", 1)[1]
        paths = paths.split("End of search list.", 1)[0]
        selected = [Path(line.strip()).resolve() for line in paths.splitlines()
                    if line.strip()]
        assert explicit.resolve() in selected, selected
        assert resource / "include" in selected, selected
        # No ambient host directories may supplement the target resource and
        # explicitly supplied include path, even when no sysroot is selected.
        assert all(path == explicit.resolve() or resource in path.parents
                   for path in selected), selected
        result = subprocess.run(command + ["-fsyntax-only"],
                                capture_output=True, text=True, env=env)
        assert result.returncode == 0, result.stderr
