import argparse
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

    def test_invalid_exception_specification(self):
        yaml_file = self.output_dir / "invalid-noexcept.yaml"
        yaml_file.write_text((self.source_dir / "input/noexcept.yaml").read_text()
                             .replace("noexcept: false", 'noexcept: "false"'))
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.run_script(yaml_file, self.output_dir / "invalid-noexcept.h")
        self.assertIn("Function noexcept must be a boolean", raised.exception.stderr)

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
