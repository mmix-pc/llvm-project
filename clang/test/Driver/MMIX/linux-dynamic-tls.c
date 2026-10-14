// REQUIRES: mmix-registered-target, lld
// RUN: split-file %s %t
// RUN: %clang --target=mmix-unknown-linux -fPIC -DMAIN -S -emit-llvm %t/access.c -o - | FileCheck %s --check-prefix=IR
// RUN: %python %t/check.py %t llvm-readobj %clang

// IR-DAG: @ld = protected thread_local(localdynamic) global i64 3
// IR-DAG: @ie = external thread_local(initialexec) global i64
// IR-DAG: @gd = external thread_local global i64
// IR-DAG: @le = thread_local(localexec) global i64 4

//--- provider.c
__thread long ie = 11, gd = 12;
// Link-only fixture, not an implementation of the resolver ABI.
void *__tls_get_addr(void *p) { return p; }

//--- access.c
extern __thread long ie __attribute__((tls_model("initial-exec")));
extern __thread long gd __attribute__((tls_model("global-dynamic")));
__thread volatile long ld
    __attribute__((visibility("protected"), tls_model("local-dynamic"))) = 3;
#ifdef MAIN
__thread volatile long le __attribute__((tls_model("local-exec"))) = 4;
#endif
long _start(void) {
  long value = ie + gd + ld;
#ifdef MAIN
  value += le;
#endif
  return value;
}

//--- check.py
import pathlib
import subprocess
import sys
root = pathlib.Path(sys.argv[1])
readobj = sys.argv[2]
clang = sys.argv[3:]
flags = ['--target=mmix-unknown-linux', '-fuse-ld=lld', '-nostdlib',
         '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-fno-exceptions']
provider = root / 'provider.so'
subprocess.run(clang + flags + ['-fPIC', '-shared', str(root / 'provider.c'),
                              '-o', str(provider)], check=True)
for role in ('shared', 'pie'):
    for opt in ('0', '2'):
        reports = []
        for lto in (False, True):
            output = root / f'{role}-o{opt}-lto{lto}'
            args = ['-fPIC', '-shared'] if role == 'shared' else ['-fPIE', '-pie', '-DMAIN']
            subprocess.run(clang + flags + args + ['-O' + opt] +
                           (['-flto'] if lto else []) +
                           [str(root / 'access.c'), str(provider), '-o', str(output)], check=True)
            rels = subprocess.check_output([readobj, '-r', str(output)], text=True)
            required = ['R_MMIX_TPOFF64 ie', 'R_MMIX_DTPMOD64 gd',
                        'R_MMIX_DTPOFF64 gd', 'R_MMIX_GLOB_DAT __tls_get_addr']
            # LLVM's generic executable policy strengthens GD/LD to IE/LE.
            if role == 'pie':
                required = ['R_MMIX_TPOFF64 ie', 'R_MMIX_TPOFF64 gd']
            assert all(r in rels for r in required), rels
            assert 'R_MMIX_TPREL_' not in rels and 'R_MMIX_TLSLD_PC64' not in rels
            reports.append(sorted(line.strip().split()[1:] for line in rels.splitlines() if 'R_MMIX_' in line))
        assert reports[0] == reports[1], reports
