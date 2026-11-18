"""Run copied Bash wrappers with argument-recording stubs, never real builds."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BuildScriptsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bash = shutil.which(os.environ.get("APO_TEST_BASH", "bash"))
        if not cls.bash:
            raise RuntimeError("Bash is required; set APO_TEST_BASH to its executable")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="apo wrappers ")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.project = self.work / "project with spaces"
        self.scripts = self.project / "scripts"
        self.scripts.mkdir(parents=True)
        for name in ("build.sh", "build-editor.sh", "run-editor.sh", "script-common.sh"):
            shutil.copyfile(ROOT / "scripts" / name, self.scripts / name)
        self.caller = self.work / "caller with spaces"
        self.caller.mkdir()
        self.stubs = self.work / "stub tools"
        self.stubs.mkdir()
        self.trace = self.work / "calls.bin"
        self.env = dict(os.environ)
        for name in ("APO_BUILD_DIR", "APO_BUILD_JOBS", "CMAKE_BUILD_PARALLEL_LEVEL",
                     "VCPKG_ROOT", "APO_QT_PREFIX", "BASH_ENV", "ENV", "CDPATH",
                     "APO_TEST_CONFIGURE_STATUS", "APO_TEST_BUILD_STATUS",
                     "APO_TEST_CTEST_STATUS", "APO_TEST_EDITOR_STATUS"):
            self.env.pop(name, None)
        self.env["PATH"] = str(self.stubs) + os.pathsep + self.env.get("PATH", "")
        self.env["APO_TEST_TRACE"] = self.bash_path(self.trace)
        for tool in ("cmake", "ctest", "editor"):
            self.write_stub(self.stubs / tool, tool)

    def bash_path(self, path):
        path = Path(path)
        if os.name != "nt":
            return str(path)
        result = subprocess.run([self.bash, "-c", 'cygpath -u -- "$1"', "convert-path", path.as_posix()],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=10, check=True)
        return result.stdout.strip()

    def write_stub(self, path, tool="editor"):
        path.parent.mkdir(parents=True, exist_ok=True)
        # NUL records preserve spaces, newlines and shell syntax as literal argv.
        path.write_text("#!/usr/bin/env bash\nset -euo pipefail\n"
                        f"printf '%s\\0' '{tool}' \"$PWD\" \"$#\" \"$@\" >> \"$APO_TEST_TRACE\"\n"
                        f"case '{tool}' in\n"
                        "cmake) if [[ ${1:-} == --build ]]; then exit \"${APO_TEST_BUILD_STATUS:-0}\"; "
                        "else exit \"${APO_TEST_CONFIGURE_STATUS:-0}\"; fi ;;\n"
                        "ctest) exit \"${APO_TEST_CTEST_STATUS:-0}\" ;;\n"
                        "editor) exit \"${APO_TEST_EDITOR_STATUS:-0}\" ;;\nesac\n", encoding="utf-8")
        path.chmod(0o755)

    def run_wrapper(self, script, *arguments, env=None):
        run_env = dict(self.env)
        if env:
            run_env.update(env)
        return subprocess.run([self.bash, self.bash_path(self.scripts / script), *map(str, arguments)],
                              cwd=self.caller, env=run_env, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=15, check=False)

    def calls(self):
        if not self.trace.exists():
            return []
        fields = self.trace.read_bytes().decode("utf-8").split("\0")
        self.assertEqual(fields.pop(), "")
        records = []
        while fields:
            tool, cwd, count = fields[:3]
            count = int(count)
            arguments = fields[3:3 + count]
            self.assertEqual(len(arguments), count)
            records.append(dict(tool=tool, cwd=cwd, argv=arguments))
            del fields[:3 + count]
        return records

    def clear_calls(self):
        self.trace.unlink(missing_ok=True)

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout)

    def assert_jobs(self, count):
        records = self.calls()
        self.assertEqual([record["tool"] for record in records], ["cmake", "cmake", "ctest"])
        self.assertEqual(records[1]["argv"][-2:], ["--parallel", str(count)])
        self.assertEqual(records[2]["argv"][-2:], ["--parallel", str(count)])

    def test_default_build_uses_two_jobs_from_any_working_directory(self):
        self.assert_success(self.run_wrapper("build.sh", "--no-open"))
        self.assert_jobs(2)
        records = self.calls()
        self.assertEqual(records[0]["argv"], ["-S", self.bash_path(self.project), "-B",
                                              self.bash_path(self.project / "build"),
                                              "-DBUILD_TESTING=ON", "-DCMAKE_BUILD_TYPE=Release"])
        self.assertTrue(all(record["cwd"] == self.bash_path(self.caller) for record in records))
        self.assertEqual(len(list((self.project / "logs").glob("build-*.txt"))), 1)
        self.assertFalse((self.caller / "logs").exists())

    def test_explicit_worker_limits_are_forwarded(self):
        for count in (1, 256):
            with self.subTest(count=count):
                self.clear_calls()
                self.assert_success(self.run_wrapper("build.sh", "--no-open", "--jobs", str(count)))
                self.assert_jobs(count)

    def test_invalid_worker_values_stop_before_any_tool(self):
        for value in ("0", "257", "-1", "1.5", "01", "", "two", "$(touch injected-jobs)"):
            with self.subTest(value=value):
                result = self.run_wrapper("build.sh", "--no-open", "--jobs", value)
                self.assertEqual(result.returncode, 2, result.stdout)
                self.assertEqual(self.calls(), [])
        self.assertFalse((self.caller / "injected-jobs").exists())

    def test_worker_environment_precedence_and_explicit_override(self):
        for env, arguments, count in (
            ({"CMAKE_BUILD_PARALLEL_LEVEL": "5"}, [], 5),
            ({"CMAKE_BUILD_PARALLEL_LEVEL": "5", "APO_BUILD_JOBS": "7"}, [], 7),
            ({"CMAKE_BUILD_PARALLEL_LEVEL": "5", "APO_BUILD_JOBS": "7"}, ["--jobs", "3"], 3),
        ):
            with self.subTest(env=env, arguments=arguments):
                self.clear_calls()
                self.assert_success(self.run_wrapper("build.sh", "--no-open", *arguments, env=env))
                self.assert_jobs(count)

    def test_relative_build_path_and_configure_arguments_remain_literal(self):
        directory = "build $(touch injected-path) with spaces"
        options = ["-DLITERAL=$(touch injected-option)", "-DQUOTED=a b;[x]*&`touch injected-backtick`",
                   "-DMULTILINE=first\nsecond", "-DCMAKE_PREFIX_PATH=/a path/Qt"]
        self.assert_success(self.run_wrapper("build.sh", "--no-open", "--build-dir", directory, "--", *options))
        records = self.calls()
        self.assertEqual(records[0]["argv"][3], self.bash_path(self.caller / directory))
        self.assertEqual(records[0]["argv"][-len(options):], options)
        self.assertEqual(records[1]["argv"][1], self.bash_path(self.caller / directory))
        self.assertEqual(records[2]["argv"][1], self.bash_path(self.caller / directory))
        for name in ("injected-path", "injected-option", "injected-backtick"):
            self.assertFalse((self.caller / name).exists())

    def test_build_stage_failures_preserve_exit_and_stop_following_tools(self):
        for variable, code, count in (("APO_TEST_CONFIGURE_STATUS", 23, 1),
                                      ("APO_TEST_BUILD_STATUS", 37, 2),
                                      ("APO_TEST_CTEST_STATUS", 19, 3)):
            with self.subTest(stage=variable):
                self.clear_calls()
                result = self.run_wrapper("build.sh", "--no-open", env={variable: str(code)})
                self.assertEqual(result.returncode, code, result.stdout)
                self.assertEqual(len(self.calls()), count)
                self.assertIn(f"Exit code: {code}", result.stdout)

    def test_empty_build_directory_and_missing_option_values_are_rejected(self):
        for arguments in (("--build-dir", ""), ("--build-dir",), ("--jobs",)):
            with self.subTest(arguments=arguments):
                result = self.run_wrapper("build.sh", "--no-open", *arguments)
                self.assertEqual(result.returncode, 2, result.stdout)
                self.assertEqual(self.calls(), [])

    def test_editor_build_wrapper_preserves_qt_prefix_and_options(self):
        prefix = "/Qt kit/$(touch injected-prefix)"
        self.assert_success(self.run_wrapper("build-editor.sh", "--no-open", "--jobs", "1",
                                             "-DAPO_ENABLE_VULKAN=OFF", env={"APO_QT_PREFIX": prefix}))
        self.assert_jobs(1)
        arguments = self.calls()[0]["argv"]
        self.assertIn("-DAPO_BUILD_EDITOR=ON", arguments)
        self.assertIn("-DCMAKE_PREFIX_PATH=" + prefix, arguments)
        self.assertIn("-DAPO_ENABLE_VULKAN=OFF", arguments)
        self.assertFalse((self.caller / "injected-prefix").exists())

    def test_editor_arguments_after_separator_are_exact(self):
        executable = self.work / "custom editor" / "program"
        self.write_stub(executable)
        arguments = ["--no-open", "a project.apoproject", "$(touch injected-editor)",
                     "apostrophe's value", "first\nsecond"]
        result = self.run_wrapper("run-editor.sh", "--exe", self.bash_path(executable),
                                  "--no-open", "--", *arguments)
        self.assert_success(result)
        self.assertEqual(self.calls(), [dict(tool="editor", cwd=self.bash_path(self.caller), argv=arguments)])
        self.assertFalse((self.caller / "injected-editor").exists())
        self.assertEqual(len(list((self.project / "logs").glob("editor-*.txt"))), 1)

    def test_editor_is_found_in_requested_build_directory(self):
        build = self.work / "selected build"
        self.write_stub(build / "apophysis-reborn")
        self.assert_success(self.run_wrapper("run-editor.sh", "--build-dir", self.bash_path(build),
                                             "--no-open", "--", "study.apoproject"))
        self.assertEqual(self.calls()[0]["argv"], ["study.apoproject"])

    def test_editor_failure_is_preserved(self):
        result = self.run_wrapper("run-editor.sh", "--exe", self.bash_path(self.stubs / "editor"),
                                  "--no-open", env={"APO_TEST_EDITOR_STATUS": "42"})
        self.assertEqual(result.returncode, 42, result.stdout)
        self.assertIn("Exit code: 42", result.stdout)
        self.assertEqual(len(self.calls()), 1)

    def test_missing_editor_fails_without_launching_a_stub(self):
        result = self.run_wrapper("run-editor.sh", "--no-open")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
