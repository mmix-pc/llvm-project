import argparse
import re
import subprocess
import sys
import unittest
from pathlib import Path


class TestHeaderGenIntegration(unittest.TestCase):
    def setUp(self):
        self.output_dir = TestHeaderGenIntegration.output_dir
        self.source_dir = Path(__file__).parent
        self.main_script = self.source_dir.parent / "main.py"
        self.maxDiff = 80 * 100

    def run_script(self, yaml_file, output_file, entry_points=[], switches=[]):
        command = [
            "python3",
            str(self.main_script),
            str(yaml_file),
            "--output",
            str(output_file),
        ] + switches

        for entry_point in entry_points:
            command.extend(["--entry-point", entry_point])

        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
        )

        print("STDOUT:", result.stdout)
        print("STDERR:", result.stderr)
        result.check_returncode()

    def compare_files(self, generated_file, expected_file):
        with generated_file.open("r") as gen_file:
            gen_content = gen_file.read()
        with expected_file.open("r") as exp_file:
            exp_content = exp_file.read()

        self.assertEqual(gen_content, exp_content)

    def test_generate_header(self):
        yaml_file = self.source_dir / "input/test_small.yaml"
        expected_output_file = self.source_dir / "expected_output/test_header.h"
        output_file = self.output_dir / "test_small.h"
        entry_points = {"func_b", "func_a", "func_c", "func_d", "func_e"}

        self.run_script(yaml_file, output_file, entry_points)

        self.compare_files(output_file, expected_output_file)

    def test_visibility_policy_and_dependency(self):
        source = self.output_dir / "visibility.yaml"
        policy = self.output_dir / "visibility-policy.yaml"
        depfile = self.output_dir / "visibility.d"
        output = self.output_dir / "visibility.h"
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text("header: example.h\nobjects:\n"
                          "  - object_name: state\n    object_type: int\n")
        policy.write_text("example.h:\n  objects:\n    state: EXT\n")
        self.run_script(source, output, switches=["--visibility", str(policy),
                                                "--depfile", str(depfile)])
        self.assertIn("#ifdef EXT\nextern int state;", output.read_text())
        self.assertIn(str(policy), depfile.read_text())
        policy.write_text("example.h:\n  objects:\n    typo: EXT\n")
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.run_script(source, output, switches=["--visibility", str(policy)])
        self.assertIn("Unknown visibility names", raised.exception.stderr)

    def test_c_wide_search_signatures(self):
        output_file = self.output_dir / "wide_search.h"
        names = ["wcschr", "wcspbrk", "wcsrchr", "wcsstr", "wmemchr"]
        self.run_script(self.source_dir.parents[2] / "include/wchar.yaml",
                        output_file, names)
        content = output_file.read_text()
        for name in names:
            self.assertIn("wchar_t *" + name + "(", content)
            self.assertNotIn("const wchar_t *" + name + "(", content)

    def test_posix_spawn_const_inputs(self):
        output_file = self.output_dir / "spawn.h"
        self.run_script(self.source_dir.parents[2] / "include/spawn.yaml",
                        output_file, ["posix_spawn"])
        self.assertIn(
            "int posix_spawn(pid_t *__restrict, const char *__restrict, "
            "const posix_spawn_file_actions_t *__restrict, "
            "const posix_spawnattr_t *__restrict, "
            "char * const * __restrict, char * const * __restrict) __NOEXCEPT;",
            output_file.read_text(),
        )

    def test_mkdtemp_declaration(self):
        output_file = self.output_dir / "mkdtemp.h"
        self.run_script(self.source_dir.parents[2] / "include/stdlib.yaml",
                        output_file, ["mkdtemp"])
        self.assertIn("char *mkdtemp(char *) __NOEXCEPT;", output_file.read_text())

    def test_strerror_r_declaration_routing(self):
        output_file = self.output_dir / "strerror_r.h"
        self.run_script(self.source_dir.parents[2] / "include/string.yaml",
                        output_file, ["strerror_r", "__xpg_strerror_r"])
        content = output_file.read_text()
        self.assertIn('#include "llvm-libc-macros/strerror-r-macros.h"', content)
        self.assertIn("#ifdef _GNU_SOURCE\nchar *strerror_r(int, char *, size_t) __NOEXCEPT;", content)
        self.assertIn("#ifdef __LIBC_USE_POSIX_STRERROR_R", content)
        self.assertIn('int strerror_r(int, char *, size_t) __NOEXCEPT __asm__("__xpg_strerror_r");', content)
        self.assertIn('int strerror_r(int, char *, size_t) __asm__("__xpg_strerror_r") __NOEXCEPT;', content)
        self.assertNotIn("#define strerror_r", content)
        self.assertNotIn("int __xpg_strerror_r(", content)
        self.run_script(self.source_dir.parents[2] / "include/string.yaml",
                        output_file, ["strerror_r"])
        self.assertNotIn('__asm__("__xpg_strerror_r")', output_file.read_text())

    def test_fcntl_macro_headers(self):
        output_file = self.output_dir / "fcntl.h"
        self.run_script(self.source_dir.parents[2] / "include/fcntl.yaml",
                        output_file, ["fcntl"])
        content = output_file.read_text()
        for name in ("fcntl-macros.h", "file-seek-macros.h", "sys-stat-macros.h"):
            self.assertIn('#include "llvm-libc-macros/' + name + '"', content)

    def test_glob_declarations(self):
        output_file = self.output_dir / "glob.h"
        self.run_script(self.source_dir.parents[2] / "include/glob.yaml",
                        output_file, ["glob", "globfree"])
        content = output_file.read_text()
        self.assertIn('#include "llvm-libc-types/glob_t.h"', content)
        self.assertIn('#include "llvm-libc-macros/glob-macros.h"', content)
        self.assertIn(
            "int glob(const char *__restrict, int, __glob_errfunc_t, "
            "glob_t *__restrict);", content)
        self.assertIn("void globfree(glob_t *) __NOEXCEPT;", content)

    def test_stdio_locking_extension(self):
        output_file = self.output_dir / "stdio_ext.h"
        predicates = ("__freadable", "__fwritable", "__freading",
                      "__fwriting", "__flbf")
        counts = ("__fbufsize", "__fpending")
        self.run_script(self.source_dir.parents[2] / "include/stdio_ext.yaml",
                        output_file, [*predicates, *counts, "__fsetlocking",
                                      "__fpurge", "_flushlbf"])
        content = output_file.read_text()
        self.assertIn('#include "llvm-libc-macros/stdio-ext-macros.h"', content)
        self.assertIn("int __fsetlocking(FILE *, int) __NOEXCEPT;", content)
        self.assertIn('#include "llvm-libc-types/size_t.h"', content)
        for name in predicates:
            self.assertIn(f"int {name}(FILE *) __NOEXCEPT;", content)
        for name in counts:
            self.assertIn(f"size_t {name}(FILE *) __NOEXCEPT;", content)
        self.assertIn("void __fpurge(FILE *) __NOEXCEPT;", content)
        self.assertIn("void _flushlbf(void);", content)

    def test_group_interfaces(self):
        output_file = self.output_dir / "grp.h"
        functions = ["getgrnam", "getgrgid", "getgrnam_r", "getgrgid_r",
                     "setgrent", "getgrent", "getgrent_r", "endgrent",
                     "fgetgrent", "fgetgrent_r", "putgrent",
                     "getgrouplist", "initgroups", "setgroups"]
        self.run_script(self.source_dir.parents[2] / "include/grp.yaml",
                        output_file, functions)
        content = output_file.read_text()
        for name in functions:
            self.assertRegex(content, rf"\b{name}\(")
        self.assertIn("struct group *getgrent(void);", content)
        self.assertIn("struct group *fgetgrent(FILE *);", content)
        self.assertIn("int putgrent(const struct group *__restrict, "
                      "FILE *__restrict);", content)
        self.assertIn("int getgrouplist(const char *, gid_t, gid_t *, int *);",
                      content)
        self.assertIn("int setgroups(size_t, const gid_t *) __NOEXCEPT;", content)
        for declaration in content.splitlines():
            if any(re.search(rf"\b{name}\(", declaration)
                   for name in functions if name != "setgroups"):
                self.assertNotIn("__NOEXCEPT", declaration)

    def test_directory_interfaces(self):
        output_file = self.output_dir / "dirent.h"
        functions = ["opendir", "fdopendir", "closedir", "dirfd", "readdir",
                     "readdir_r", "rewinddir", "seekdir", "telldir", "scandir",
                     "scandirat", "alphasort", "versionsort", "posix_getdents"]
        self.run_script(self.source_dir.parents[2] / "include/dirent.yaml",
                        output_file, functions)
        content = output_file.read_text()
        for name in functions:
            self.assertRegex(content, rf"\b{name}\(")
        self.assertIn("int scandir(const char *, struct dirent ***, "
                      "__dirent_filter_t, __dirent_compare_t);", content)
        self.assertIn("int scandirat(int, const char *, struct dirent ***, "
                      "__dirent_filter_t, __dirent_compare_t);", content)
        self.assertIn("ssize_t posix_getdents(int, void *, size_t, int);", content)
        self.assertIn("void seekdir(DIR *, long) __NOEXCEPT;", content)
        self.assertIn("long telldir(DIR *) __NOEXCEPT;", content)
        self.assertNotIn("struct_dirent_struct_dirent.h", content)

    def test_readv_exception_specification(self):
        output_file = self.output_dir / "readv.h"
        self.run_script(self.source_dir.parents[2] / "include/sys/uio.yaml",
                        output_file, ["readv"])
        self.assertIn("ssize_t readv(int, const struct iovec *, int);",
                      output_file.read_text())

    def test_system_exception_specification(self):
        output_file = self.output_dir / "system.h"
        self.run_script(self.source_dir.parents[2] / "include/stdlib.yaml",
                        output_file, ["system"])
        self.assertIn("int system(const char *);", output_file.read_text())

    def test_writev_exception_specification(self):
        output_file = self.output_dir / "writev.h"
        self.run_script(self.source_dir.parents[2] / "include/sys/uio.yaml",
                        output_file, ["writev"])
        self.assertIn("ssize_t writev(int, const struct iovec *, int);",
                      output_file.read_text())

    def test_sendmsg_exception_specification(self):
        output_file = self.output_dir / "sendmsg.h"
        self.run_script(self.source_dir.parents[2] / "include/sys/socket.yaml",
                        output_file, ["sendmsg"])
        declaration = next(line for line in output_file.read_text().splitlines()
                           if "sendmsg(" in line)
        self.assertNotIn("__NOEXCEPT", declaration)

    def test_socket_cancellation_exception_specifications(self):
        functions = ("accept", "accept4", "connect", "recv", "recvfrom",
                     "recvmsg", "recvmmsg", "send", "sendto", "sendmsg",
                     "sendmmsg")
        output_file = self.output_dir / "socket-cancellation.h"
        self.run_script(self.source_dir.parents[2] / "include/sys/socket.yaml",
                        output_file, list(functions))
        lines = output_file.read_text().splitlines()
        for function in functions:
            declaration = next(line for line in lines if function + "(" in line)
            self.assertNotIn("__NOEXCEPT", declaration)

    def test_exception_specifications(self):
        output_file = self.output_dir / "noexcept.h"
        self.run_script(self.source_dir / "input/noexcept.yaml", output_file)
        content = output_file.read_text()
        self.assertIn("void default_nothrow(void) __NOEXCEPT;", content)
        self.assertIn("void explicit_nothrow(void) __NOEXCEPT;", content)
        self.assertIn("void may_throw(void);", content)
        self.assertIn("void guarded_may_throw(void);", content)
        self.assertIn("#ifdef ENABLE_THROWING_CALLBACK", content)

    def test_pthread_once_exception_specification(self):
        output_file = self.output_dir / "pthread.h"
        self.run_script(self.source_dir.parents[2] / "include/pthread.yaml",
                        output_file, ["pthread_once", "pthread_self"])
        content = output_file.read_text()
        self.assertIn("int pthread_once(pthread_once_t *, __pthread_once_func_t);", content)
        self.assertIn("pthread_t pthread_self(void) __NOEXCEPT;", content)

    def test_pthread_mutex_timeout_declarations(self):
        output_file = self.output_dir / "pthread-timeouts.h"
        self.run_script(self.source_dir.parents[2] / "include/pthread.yaml",
                        output_file, ["pthread_mutex_timedlock", "pthread_mutex_clocklock"])
        content = output_file.read_text()
        for declaration in (
            "int pthread_mutex_timedlock(pthread_mutex_t *__restrict, const struct timespec *__restrict) __NOEXCEPT;",
            "int pthread_mutex_clocklock(pthread_mutex_t *__restrict, clockid_t, const struct timespec *__restrict) __NOEXCEPT;",
        ):
            with self.subTest(declaration=declaration):
                self.assertIn(declaration, content)

    def test_pthread_barrier_unsigned_count(self):
        output_file = self.output_dir / "pthread-barrier.h"
        self.run_script(self.source_dir.parents[2] / "include/pthread.yaml",
                        output_file, ["pthread_barrier_init"])
        content = output_file.read_text()
        self.assertIn(
            "int pthread_barrier_init(pthread_barrier_t *__restrict, const pthread_barrierattr_t *__restrict, unsigned) __NOEXCEPT;",
            content,
        )

    def test_pthread_exit_exception_specification(self):
        output_file = self.output_dir / "pthread.h"
        self.run_script(self.source_dir.parents[2] / "include/pthread.yaml",
                        output_file, ["pthread_exit", "pthread_self"])
        content = output_file.read_text()
        self.assertIn("_Noreturn void pthread_exit(void *);", content)
        self.assertIn("pthread_t pthread_self(void) __NOEXCEPT;", content)

    def test_stdlib_comparator_exception_specifications(self):
        output_file = self.output_dir / "stdlib.h"
        self.run_script(
            self.source_dir.parents[2] / "include/stdlib.yaml",
            output_file,
            ["bsearch", "qsort", "qsort_r", "abs"],
        )
        content = output_file.read_text()
        for declaration in (
            "void *bsearch(const void *, const void *, size_t, size_t, __search_compare_t);",
            "void qsort(void *, size_t, size_t, __qsortcompare_t);",
            "void qsort_r(void *, size_t, size_t, __qsortrcompare_t, void *);",
        ):
            with self.subTest(declaration=declaration):
                self.assertIn(declaration, content)
        self.assertIn("int abs(int) __NOEXCEPT;", content)

    def test_sleep_exception_specifications(self):
        output_file = self.output_dir / "unistd.h"
        self.run_script(self.source_dir.parents[2] / "include/unistd.yaml",
                        output_file, ["sleep", "usleep", "getpid"])
        content = output_file.read_text()
        self.assertIn("unsigned int sleep(unsigned int);", content)
        self.assertIn("int usleep(useconds_t);", content)
        self.assertIn("int getpid(void) __NOEXCEPT;", content)

    def test_invalid_exception_specification(self):
        yaml_file = self.output_dir / "invalid-noexcept.yaml"
        yaml_file.write_text((self.source_dir / "input/noexcept.yaml").read_text()
                             .replace("noexcept: false", 'noexcept: "false"'))
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.run_script(yaml_file, self.output_dir / "invalid-noexcept.h")
        self.assertIn("Function noexcept must be a boolean", raised.exception.stderr)

    def test_blocking_syscall_exception_specifications(self):
        for header, functions in (
            ("unistd", ["read", "write", "close"]),
            ("fcntl", ["open", "openat"]),
            ("time", ["nanosleep"]),
            ("pthread", ["pthread_join", "pthread_cond_wait",
                         "pthread_cond_timedwait", "pthread_cond_clockwait",
                         "pthread_cancel", "pthread_setcancelstate", "pthread_setcanceltype"]),
            ("stdio", ["fopen", "fclose", "fread", "fwrite", "fflush", "fseek",
                       "ftell", "fseeko", "ftello", "fgetc", "fputc", "fgets",
                       "fputs", "getc", "putc", "puts", "printf", "fprintf",
                       "vfprintf", "fread_unlocked", "fwrite_unlocked",
                       "fscanf", "vfscanf", "vprintf", "getchar", "putchar",
                       "freopen"]),
            ("wchar", ["getwc", "fputwc", "fgetwc", "fgetws", "fputws",
                       "getwchar", "putwc", "putwchar", "fwprintf",
                       "vfwprintf", "wprintf", "vwprintf", "fwscanf",
                       "vfwscanf", "wscanf", "vwscanf"]),
            ("dirent", ["opendir", "readdir", "closedir"]),
        ):
            with self.subTest(header=header):
                output = self.output_dir / (header + ".h")
                self.run_script(self.source_dir.parents[2] /
                                ("include/" + header + ".yaml"), output, functions)
                declarations = [line for line in output.read_text().splitlines()
                                if any(name + "(" in line for name in functions)]
                self.assertEqual(len(declarations), len(functions))
                self.assertTrue(all("__NOEXCEPT" not in line
                                    for line in declarations))

    def test_minimal_fork_declaration(self):
        output = self.output_dir / "minimal-fork.h"
        self.run_script(self.source_dir.parents[2] / "include/unistd.yaml",
                        output, ["_Fork"])
        text = output.read_text()
        self.assertIn("pid_t _Fork(void) __NOEXCEPT;", text)
        self.assertNotIn("pid_t fork(", text)

    def test_generate_subdir_header(self):
        yaml_file = self.source_dir / "input" / "subdir" / "test.yaml"
        expected_output_file = self.source_dir / "expected_output" / "subdir" / "test.h"
        output_file = self.output_dir / "subdir" / "test.h"
        self.run_script(yaml_file, output_file)
        self.compare_files(output_file, expected_output_file)

    def test_custom_license_and_standards(self):
        yaml_file = self.source_dir / "input" / "custom.yaml"
        expected_output_file = self.source_dir / "expected_output" / "custom.h"
        output_file = self.output_dir / "custom.h"
        self.run_script(yaml_file, output_file)
        self.compare_files(output_file, expected_output_file)

    def test_generate_json(self):
        yaml_file = self.source_dir / "input/test_small.yaml"
        expected_output_file = self.source_dir / "expected_output/test_small.json"
        output_file = self.output_dir / "test_small.json"

        self.run_script(yaml_file, output_file, switches=["--json"])

        self.compare_files(output_file, expected_output_file)

    def test_sorting(self):
        yaml_file = self.source_dir / "input" / "sorting.yaml"
        expected_output_file = self.source_dir / "expected_output" / "sorting.h"
        output_file = self.output_dir / "sorting.h"
        self.run_script(yaml_file, output_file)
        self.compare_files(output_file, expected_output_file)

    def test_generate_proxy_header(self):
        yaml_file = self.source_dir / "input/test_small.yaml"
        expected_output_file = self.source_dir / "expected_output/test_small_proxy.h"
        output_file = self.output_dir / "test_small.h"
        self.run_script(yaml_file, output_file, switches=["--proxy"])
        self.compare_files(output_file, expected_output_file)

    def test_generate_macro_only_header(self):
        yaml_file = self.source_dir / "input/macro_only.yaml"
        expected_output_file = self.source_dir / "expected_output/macro_only.h"
        output_file = self.output_dir / "macro_only.h"
        self.run_script(yaml_file, output_file)
        self.compare_files(output_file, expected_output_file)

    def test_type_guarding(self):
        yaml_file = self.source_dir / "input/type_guarding.yaml"
        expected_output_file = self.source_dir / "expected_output/type_guarding.h"
        output_file = self.output_dir / "type_guarding.h"
        self.run_script(yaml_file, output_file)
        self.compare_files(output_file, expected_output_file)

    def test_func_guarding(self):
        yaml_file = self.source_dir / "input/func_guarding.yaml"
        expected_output_file = self.source_dir / "expected_output/func_guarding.h"
        output_file = self.output_dir / "func_guarding.h"
        self.run_script(yaml_file, output_file)
        self.compare_files(output_file, expected_output_file)


def main():
    parser = argparse.ArgumentParser(description="TestHeaderGenIntegration arguments")
    parser.add_argument(
        "--output_dir",
        type=Path,
        help="Output directory for generated headers",
        required=True,
    )
    args, remaining_argv = parser.parse_known_args()

    TestHeaderGenIntegration.output_dir = args.output_dir

    sys.argv[1:] = remaining_argv

    unittest.main()


if __name__ == "__main__":
    main()
