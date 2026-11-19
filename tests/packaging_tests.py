"""Runtime inventory, path admission, PE dependency closure and archive checks.

These tests run on the build host; they do not claim Windows installer execution.
The separate verify-installer-windows.ps1 script exercises the Windows lifecycle.
"""
import hashlib
import importlib.util
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


package = module("packaging_runtime_tests", ROOT / "scripts/package-runtime.py")
installer = module("packaging_installer_tests", ROOT / "packaging/build_installer.py")


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="apo-package-tests-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def put(self, name, data=b"fixture bytes", root=None):
        path = (root or self.root) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def fixture_payload(self, extra=None):
        payload = self.root / "apophysis-reborn-0.10.0-windows-x64"
        payload.mkdir()
        for name, data in {"apophysis-reborn.exe": b"fixture editor, not executable",
                           "flame-cpu.exe": b"fixture CLI, not executable",
                           "examples/normal file.flame": b"<flame/>\n", **(extra or {})}.items():
            self.put(name, data, payload)
        metadata = {"schema": package.SCHEMA, "schema_version": 1, "version": "0.10.0", "edition": "Windows x64 runtime",
                    "source_commit": "a" * 40, "files": package.manifest_files(payload)}
        self.write_manifest(payload, metadata)
        return payload

    def write_manifest(self, payload, metadata):
        (payload / "package-manifest.json").write_text(json.dumps(metadata, ensure_ascii=True), encoding="utf-8")
        files = package.manifest_files(payload)
        files.pop("SHA256SUMS.txt", None)
        (payload / "SHA256SUMS.txt").write_text("".join(record["sha256"] + "  " + name + "\n"
                                                       for name, record in files.items()), encoding="utf-8")

    def fixture_runtime(self):
        runtime = self.root / "runtime"
        imports = {
            "apophysis-reborn.exe": ({"qt6widgets.dll": {"widget"}, "kernel32.dll": {"Sleep"}}, set()),
            "flame-cpu.exe": ({"libc++.dll": {"allocate"}}, set()),
            "apophysis-mcp.exe": ({"qt6network.dll": {"local_socket"}}, set()),
            "Qt6Network.dll": ({"qt6core.dll": {"object"}}, {"local_socket"}),
            "platforms/qwindows.dll": ({"qt6widgets.dll": {"widget"}}, set()),
            "platforms/qoffscreen.dll": ({"qt6widgets.dll": {"widget"}}, set()),
            "styles/qmodernwindowsstyle.dll": ({"qt6widgets.dll": {"widget"}}, set()),
            "imageformats/qjpeg.dll": ({"libjpeg.dll": {"decode"}}, set()),
            "plugins/reborn-wave/reborn_wave.dll": ({"libc++.dll": {"allocate"}}, {"apo_plugin_entry"}),
            "Qt6Widgets.dll": ({"qt6core.dll": {"object"}}, {"widget"}),
            "Qt6Core.dll": ({"libc++.dll": {"allocate"}}, {"object"}),
            "libc++.dll": ({"api-ms-win-crt-runtime-l1-1-0.dll": {"memcpy"}}, {"allocate"}),
            "libjpeg.dll": ({}, {"decode"}),
            "Qt6Test.dll": ({"missing-test-dependency.dll": set()}, {"test"}),
            "editor-tests.exe": ({"qt6test.dll": {"test"}}, set()),
            "tests/plugin-fixture/Qt6Core.dll": ({"fixture-only.dll": set()}, {"not_object"}),
        }
        for name in imports:
            self.put(name, root=runtime)
        return runtime, imports

    def closure(self, runtime, imports):
        with mock.patch.object(package.stage, "inspect", side_effect=lambda _tool, path: imports[path.relative_to(runtime).as_posix()]):
            return package.native_closure(runtime, "unused-test-objdump")

    def test_windows_safe_names_and_untrusted_path_boundaries(self):
        invalid = ("", "/outside", "../outside", "a/../../outside", "C:/file", "a:b", "a\\b", "a//b", "./a",
                   "a/./b", "a/../b", "a/", "a.", "a ", "AUX.txt", "com1.bin", "LPT9", "COM\u00b9.txt",
                   "lpt\u00b2", "CONIN$", "a\nfile", "a\x00file", 'a"file', "a?b", "a*b", "a|b", "a<b", "a>b",
                   "a" * 151, "a/" + "\U0001f30c" * 75)
        for name in invalid:
            with self.subTest(name=name), self.assertRaises(ValueError):
                package.safe_relative(name)
        for name in ("examples/normal file.flame", "docs/Fran\u00e7ais.txt", "images/\U0001f30c.txt", "dollar$file.txt"):
            self.assertEqual(package.safe_relative(name).as_posix(), name)

    def test_windows_file_directory_and_reserved_output_conflicts(self):
        source = self.put("source")
        for first, second in (("Readme.txt", "README.TXT"), ("folder", "Folder/child"), ("folder/child", "FOLDER")):
            files = {}
            package.add_file(files, source, first)
            with self.subTest(first=first, second=second), self.assertRaisesRegex(ValueError, "collide"):
                package.add_file(files, source, second)
        for name in ("package-manifest.json", "PACKAGE-MANIFEST.JSON", "SHA256SUMS.txt"):
            with self.assertRaisesRegex(ValueError, "Reserved"):
                package.add_file({}, source, name)

    def test_input_and_ancestor_symlinks_cannot_escape_payload(self):
        source = self.put("real/file.txt", b"private")
        link = self.root / "linked"
        try:
            link.symlink_to(source.parent, target_is_directory=True)
            (self.root / "linked-file").symlink_to(source)
        except (OSError, NotImplementedError):
            self.skipTest("This account cannot create symbolic links")
        for path in (link / "file.txt", self.root / "linked-file"):
            with self.assertRaisesRegex(ValueError, "Symbolic-link"):
                package.regular(path)
        with self.assertRaisesRegex(ValueError, "Symbolic-link"):
            package.add_tree({}, self.root, "docs")

    def test_windows_reparse_attributes_are_rejected_even_without_symlink_flag(self):
        source = self.put("junction/file")
        original = Path.stat
        junction = source.parent

        def mocked_stat(path, *args, **kwargs):
            value = original(path, *args, **kwargs)
            if path == junction:
                return mock.Mock(st_file_attributes=0x400, st_mode=value.st_mode)
            return value

        with mock.patch.object(Path, "stat", mocked_stat), self.assertRaisesRegex(ValueError, "Reparse-point"):
            package.regular(source)

    def test_dependency_closure_omits_test_only_modules_and_resolves_exports(self):
        runtime, imports = self.fixture_runtime()
        files, audit = self.closure(runtime, imports)
        self.assertEqual(len(files), 13)
        self.assertIn("apophysis-mcp.exe", files)
        self.assertIn("Qt6Network.dll", files)
        self.assertNotIn("Qt6Test.dll", files)
        self.assertNotIn("editor-tests.exe", files)
        self.assertNotIn("tests/plugin-fixture/Qt6Core.dll", files)
        self.assertIn("plugins/reborn-wave/reborn_wave.dll", files)
        self.assertEqual(audit["non_system_imported_symbols_resolved"], 11)
        self.assertEqual(audit["windows_system_dependencies"], ["api-ms-win-crt-runtime-l1-1-0.dll", "kernel32.dll"])
        self.assertIs(audit["windows_executed"], False)

    def test_missing_import_and_export_are_hard_errors(self):
        runtime, imports = self.fixture_runtime()
        imports["libc++.dll"] = ({"not-installed.dll": {"needed"}}, {"allocate"})
        with self.assertRaisesRegex(ValueError, "Missing runtime DLL"):
            self.closure(runtime, imports)
        imports["libc++.dll"] = ({}, {"different_export"})
        with self.assertRaisesRegex(ValueError, "Missing imported symbols"):
            self.closure(runtime, imports)

    def test_ambiguous_dll_names_and_bundled_vulkan_loader_are_refused(self):
        runtime, imports = self.fixture_runtime()
        duplicate = self.put("extra/QT6CORE.DLL", root=runtime)
        with self.assertRaisesRegex(ValueError, "Ambiguous"):
            self.closure(runtime, imports)
        duplicate.unlink()
        self.put("vulkan-1.dll", root=runtime)
        imports["vulkan-1.dll"] = ({}, {"vkGetInstanceProcAddr"})
        imports["flame-cpu.exe"] = ({"vulkan-1.dll": {"vkGetInstanceProcAddr"}}, set())
        with self.assertRaisesRegex(ValueError, "must not be redistributed"):
            self.closure(runtime, imports)

    def test_exact_inventory_round_trip_and_cli_verification(self):
        payload = self.fixture_payload()
        metadata, files = package.verify_payload(payload)
        self.assertEqual(len(files), len(metadata["files"]) + 2)
        self.assertEqual(files["package-manifest.json"]["sha256"], package.digest(payload / "package-manifest.json"))
        result = subprocess.run([sys.executable, str(ROOT / "scripts/package-runtime.py"), "--verify", str(payload)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["verified_files"], 5)
        self.assertIs(json.loads(result.stdout)["windows_executed"], False)

    def test_modified_missing_and_unlisted_payload_files_are_detected(self):
        payload = self.fixture_payload()
        path = payload / "flame-cpu.exe"
        original = path.read_bytes()
        path.write_bytes(original + b"changed")
        with self.assertRaisesRegex(ValueError, "changed"):
            package.verify_payload(payload)
        path.unlink()
        with self.assertRaisesRegex(ValueError, "inventory"):
            package.verify_payload(payload)
        path.write_bytes(original)
        self.put("unlisted.dat", root=payload)
        with self.assertRaisesRegex(ValueError, "inventory"):
            package.verify_payload(payload)

    def test_manifest_checksum_and_source_identity_tampering_are_detected(self):
        payload = self.fixture_payload()
        path = payload / "package-manifest.json"
        original = path.read_bytes()
        metadata = json.loads(original)
        for key, value in (("schema", "wrong"), ("schema_version", 2), ("version", "0.10"), ("source_commit", "abcd")):
            changed = dict(metadata, **{key: value})
            path.write_text(json.dumps(changed))
            with self.subTest(key=key), self.assertRaises(ValueError):
                package.verify_payload(payload)
        path.write_bytes(original + b" ")
        with self.assertRaisesRegex(ValueError, "checksum failed"):
            package.verify_payload(payload)

    def test_case_collisions_and_nonportable_inventory_names_are_rejected(self):
        payload = self.fixture_payload()
        metadata = json.loads((payload / "package-manifest.json").read_text())
        for name in ("APOPHYSIS-REBORN.EXE", "aux.txt", "bad."):
            path = self.put(name, root=payload)
            metadata["files"][name] = {"sha256": package.digest(path), "bytes": path.stat().st_size}
            self.write_manifest(payload, metadata)
            with self.subTest(name=name), self.assertRaises(ValueError):
                package.verify_payload(payload)
            del metadata["files"][name]
            path.unlink()

    def test_checksum_duplicates_and_missing_coverage_are_detected(self):
        payload = self.fixture_payload()
        path = payload / "SHA256SUMS.txt"
        original = path.read_text()
        first = original.splitlines(keepends=True)[0]
        for changed in (original + first, original.replace(first, ""), "0" * 64 + first[64:] + original[len(first):]):
            path.write_text(changed)
            with self.subTest(changed=changed[:20]), self.assertRaises(ValueError):
                package.verify_payload(payload)

    def test_portable_zip_bytes_are_reproducible_and_entries_match_hashes(self):
        payload = self.fixture_payload({"docs/space $ \u00e9 \U0001f30c.txt": b"Unicode filename"})
        first, second = self.root / "first.zip", self.root / "second.zip"
        package.portable_zip(payload, first, 1700000000)
        for path in package.tree_files(payload):
            os.utime(path, (1800000000, 1800000000))
        package.portable_zip(payload, second, 1700000000)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(first) as archive:
            expected = package.manifest_files(payload)
            self.assertEqual(len(archive.infolist()), len(expected))
            self.assertIsNone(archive.testzip())
            for name, record in expected.items():
                entry = payload.name + "/" + name
                self.assertEqual(hashlib.sha256(archive.read(entry)).hexdigest(), record["sha256"])
                self.assertEqual(archive.getinfo(entry).external_attr >> 16, 0o100644)

    def test_corresponding_sources_require_exact_checksums_and_inventory(self):
        source = self.put("sources/qt6-base-source.tar.zst", b"source archive fixture")
        sums = source.parent / "SHA256SUMS.txt"
        sums.write_text(package.digest(source) + "  " + source.name + "\n")
        package.check_lgpl_sources(source.parent)
        source.write_bytes(b"tampered source")
        with self.assertRaisesRegex(ValueError, "changed"):
            package.check_lgpl_sources(source.parent)
        sums.write_text(package.digest(source) + "  " + source.name + "\n")
        self.put("unlisted.tar.zst", root=source.parent)
        with self.assertRaisesRegex(ValueError, "Unlisted"):
            package.check_lgpl_sources(source.parent)

    def test_installer_generated_inputs_keep_paths_literal_and_hashes_compiled(self):
        payload = self.fixture_payload({"docs/space $ \u00e9 \U0001f30c.txt": b"document"})
        metadata, files = package.verify_payload(payload)
        include = installer.generate_include(files)
        self.assertIn('File "/oname=space ${U+24}${U+24} \u00e9 \U0001f30c.txt"', include)
        self.assertNotIn(str(payload), include)
        self.assertEqual(include.count('"staged-inputs/'), len(files))
        header = installer.generate_header(payload, metadata, files)
        self.assertIn('L"docs\\\\space $ \\u00e9 \\U0001f30c.txt"', header)
        for record in files.values(): self.assertIn(record["sha256"], header)
        self.assertIn(package.digest(payload / "package-manifest.json"), header)
        self.assertEqual(installer.nsis_string('$name"quoted"'), '${U+24}${U+24}name$\\"quoted$\\"')
        self.assertEqual(installer.nsis_string('${APO_VERSION} $%PATH%'),
                         '${U+24}${U+24}{APO_VERSION} ${U+24}${U+24}%PATH%')
        for name in ("line\nbreak", "carriage\rreturn", "nul\0byte"):
            with self.assertRaises(ValueError): installer.nsis_string(name)

    @unittest.skipUnless(shutil.which("c++"), "host C++ compiler needed for installer rule tests")
    def test_upgrade_rules_and_old_release_receipt(self):
        name = "examples/a study \u00e9 \U0001f30c.flame"
        payload = self.fixture_payload({name: b"edited by the release author"})
        executable = self.root / ("upgrade-test.exe" if os.name == "nt" else "upgrade-test")
        subprocess.run(["c++", "-std=c++20", "-I", str(ROOT / "packaging"),
                        str(ROOT / "tests/installer_upgrade_tests.cpp"), "-o", str(executable)], check=True)
        subprocess.run([str(executable), str(payload / "package-manifest.json"),
                        package.digest(payload / name)], check=True)

    def test_installer_refuses_existing_output_before_invoking_build_tools(self):
        payload = self.fixture_payload()
        output = self.put("existing.exe", b"preserve existing installer")
        result = subprocess.run([sys.executable, str(ROOT / "packaging/build_installer.py"), "--payload", str(payload),
                                 "--output", str(output), "--compiler", "tool-that-does-not-exist"], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("new .exe", result.stderr)
        self.assertEqual(output.read_bytes(), b"preserve existing installer")


if __name__ == "__main__":
    unittest.main()
