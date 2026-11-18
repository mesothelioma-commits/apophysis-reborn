"""Exercise portable staging paths and native-module dependency collection.

Uses a synthetic PE inspection result; it never executes a Windows binary.
"""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "stage_editor_runtime", Path(__file__).resolve().parents[1] / "scripts/stage_editor_runtime.py")
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


class StagingTests(unittest.TestCase):
    def test_portable_relative_paths(self):
        self.assertEqual(stage.safe_relative("plugins/wave/reborn_wave.dll"),
                         Path("plugins/wave/reborn_wave.dll"))
        for value in ("", "/outside", "../outside", "nested/../outside", "nested//file",
                      "C:/outside", "C:outside", "\\server\\share", "nested\\file", ".",
                      "nested/./file", "file.", "folder /file", "NUL.txt", "dir/COM1",
                      "dir/bad?.txt", "dir/bad\x00.txt"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                stage.safe_relative(value)

    def fixtures(self, root):
        qt = root / "qt"
        runtime = root / "runtime"
        runtime.mkdir()
        for name in ("platforms/qwindows.dll", "platforms/qoffscreen.dll",
                     "styles/qmodernwindowsstyle.dll", "imageformats/qjpeg.dll"):
            p = qt / "plugins" / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"Qt plugin fixture")
        (qt / "bin").mkdir()
        (qt / "bin/Qt6Core.dll").write_bytes(b"Qt core fixture")
        source = root / "source"
        source.mkdir()
        for name in ("app.exe", "wave.dll", "fixture.dll", "helper.dll"):
            (source / name).write_bytes(name.encode())
        (source / "wave.apoplugin").write_text("<manifest/>", encoding="utf-8")
        arguments = ["stage_editor_runtime.py", "--qt-prefix", str(qt),
                     "--compiler-runtime", str(runtime), "--objdump", "unused-objdump",
                     "--output", str(root / "output"), str(source / "app.exe"),
                     "--module", str(source / "wave.dll"), "plugins/wave/wave.dll",
                     "--module", str(source / "fixture.dll"), "tests/plugins/fixture.dll",
                     "--data", str(source / "wave.apoplugin"), "plugins/wave/wave.apoplugin"]
        return arguments, source

    @staticmethod
    def inspect(_objdump, path):
        if path.name == "Qt6Core.dll":
            return {}, {"qt_entry"}
        if path.name == "helper.dll":
            return {}, {"helper_entry"}
        if path.name == "wave.dll":
            return {"helper.dll": {"helper_entry"}, "kernel32.dll": {"GetLastError"}}, {"apo_plugin_query"}
        return {"qt6core.dll": {"qt_entry"}}, set()

    def run_stage(self, arguments):
        with patch.object(sys, "argv", arguments), patch.object(stage, "inspect", self.inspect), \
                contextlib.redirect_stdout(io.StringIO()):
            stage.main()

    def test_module_roots_data_and_adjacent_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            arguments, _ = self.fixtures(root)
            self.run_stage(arguments)
            output = root / "output"
            self.assertEqual((output / "plugins/wave/wave.dll").read_bytes(), b"wave.dll")
            self.assertEqual((output / "plugins/wave/wave.apoplugin").read_text(), "<manifest/>")
            self.assertEqual((output / "tests/plugins/fixture.dll").read_bytes(), b"fixture.dll")
            self.assertEqual((output / "helper.dll").read_bytes(), b"helper.dll")
            manifest = json.loads((output / "runtime-dependencies.json").read_text())
            self.assertEqual(manifest["data_files"], ["plugins/wave/wave.apoplugin"])
            self.assertIn("plugins/wave/wave.dll", manifest["files"])
            self.assertIn("kernel32.dll", manifest["windows_system_dependencies"])
            self.assertEqual((output / "qt.conf").read_text(), "[Paths]\nPrefix=.\nPlugins=.\n")

    def test_reserved_destination_rejects_before_any_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            arguments, source = self.fixtures(root)
            arguments += ["--data", str(source / "wave.apoplugin"), "QT.CONF"]
            with self.assertRaises(ValueError):
                self.run_stage(arguments)
            self.assertFalse((root / "output/app.exe").exists())

    def test_ancestor_destination_rejects_before_any_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            arguments, source = self.fixtures(root)
            arguments += ["--data", str(source / "wave.apoplugin"), "plugins"]
            with self.assertRaises(ValueError):
                self.run_stage(arguments)
            self.assertFalse((root / "output/app.exe").exists())

    def test_symlink_destination_cannot_escape_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            arguments, _ = self.fixtures(root)
            (root / "output").mkdir()
            (root / "outside").mkdir()
            try:
                (root / "output/plugins").symlink_to(root / "outside", target_is_directory=True)
            except OSError as error:
                self.skipTest(f"Symlinks unavailable: {error}")
            with self.assertRaises(ValueError):
                self.run_stage(arguments)
            self.assertFalse((root / "output/app.exe").exists())
            self.assertEqual(list((root / "outside").iterdir()), [])


if __name__ == "__main__":
    unittest.main()
