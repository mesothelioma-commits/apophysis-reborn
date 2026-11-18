"""Source release admission, reproducibility, restoration and tamper checks."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("apo_source_package", Path(__file__).resolve().parents[1] / "scripts/package-source.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


@unittest.skipUnless(shutil.which("git"), "Git is required for source packaging")
class SourcePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="apo-source-tests-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / "checkout"
        self.repo.mkdir()
        self.run_git("init", "--quiet", "-b", "main")
        (self.repo / "src").mkdir()
        (self.repo / "scripts").mkdir()
        (self.repo / "CMakeLists.txt").write_text("project(apophysis_reborn VERSION 0.10.0 LANGUAGES CXX)\n")
        (self.repo / "src/build_info.cpp").write_text("// build identity fixture\n")
        (self.repo / "scripts/build.sh").write_text("#!/bin/sh\nexit 0\n")
        (self.repo / "scripts/build.sh").chmod(0o755)
        self.commit()
        self.run_git("tag", "v0.9.0-example")
        (self.repo / "README.md").write_text("Release source fixture.\n")
        self.commit()
        self.tag = "v0.10.0-release-hardening"
        self.run_git("tag", "-a", self.tag, "-m", "release")
        self.output = self.root / "release.zip"

    def run_git(self, *args, repo=None):
        env = os.environ.copy()
        env.update(GIT_AUTHOR_DATE="2026-01-01T00:00:00Z", GIT_COMMITTER_DATE="2026-01-01T00:00:00Z")
        return subprocess.check_output(["git", "-c", "user.name=Source package tests", "-c", "user.email=tests@example.invalid",
                                        "-C", str(repo or self.repo), *args], env=env, stderr=subprocess.PIPE, text=True).strip()

    def commit(self):
        self.run_git("add", ".")
        self.run_git("commit", "--quiet", "-m", "fixture")

    def export(self, output=None):
        return package.package_source(self.repo, output or self.output, self.tag)

    def extracted(self):
        directory = self.root / "extracted"
        with zipfile.ZipFile(self.output) as archive:
            archive.extractall(directory)
        return directory / "apophysis-reborn"

    def test_round_trip_restores_sources_permissions_and_full_history(self):
        result = self.export()
        revision = self.run_git("rev-parse", "HEAD")
        self.assertEqual(result["source_revision"], revision)
        stage = self.extracted()
        identity, entries = package.verify_source_identity(stage)
        self.assertEqual(identity["version"], "0.10.0")
        self.assertIs(identity["source_dirty"], False)
        self.assertEqual(set(entries), {"CMakeLists.txt", "src/build_info.cpp", "scripts/build.sh", "README.md"})
        self.assertNotIn(package.BUNDLE, entries)
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.getinfo("apophysis-reborn/scripts/build.sh").external_attr >> 16 & 0o777, 0o755)
            self.assertEqual(archive.getinfo("apophysis-reborn/README.md").external_attr >> 16 & 0o777, 0o644)
        sums = dict(line.split("  ", 1)[::-1] for line in (stage / "SHA256SUMS.txt").read_text().splitlines())
        actual = {p.relative_to(stage).as_posix() for p in stage.rglob("*") if p.is_file()}
        self.assertEqual(set(sums), actual - {"SHA256SUMS.txt"})
        for name, digest in sums.items():
            self.assertEqual(package.sha256(stage / name), digest)
        clone = self.root / "restored"
        self.run_git("clone", "--quiet", str(stage / package.BUNDLE), str(clone))
        self.assertEqual(self.run_git("rev-parse", "HEAD", repo=clone), revision)
        self.assertEqual(self.run_git("rev-list", "--all", "--count", repo=clone), "2")
        self.assertEqual(self.run_git("tag", "--list", repo=clone).splitlines(), ["v0.10.0-release-hardening", "v0.9.0-example"])
        metadata = (stage / "source-package.json").read_text()
        self.assertNotIn(str(self.root), metadata)
        self.assertNotIn(str(self.root), (stage / package.IDENTITY).read_text())

    def test_reproducible_bytes_for_unchanged_source_refs(self):
        first = self.export()
        second = self.export(self.root / "second.zip")
        self.assertEqual(first["sha256"], second["sha256"])
        self.assertEqual(self.output.read_bytes(), (self.root / "second.zip").read_bytes())

    def test_dirty_untracked_and_wrong_tag_are_refused(self):
        with self.assertRaisesRegex(ValueError, "does not identify HEAD"):
            package.package_source(self.repo, self.output, "v0.9.0-example")
        with self.assertRaisesRegex(ValueError, "version differs"):
            package.package_source(self.repo, self.output, self.tag, "9.9.9")
        (self.repo / "README.md").write_text("modified")
        with self.assertRaisesRegex(ValueError, "clean"):
            self.export()
        self.run_git("checkout", "--", "README.md")
        (self.repo / "untracked.txt").write_text("untracked")
        with self.assertRaisesRegex(ValueError, "clean"):
            self.export()
        self.assertFalse(self.output.exists())

    def test_existing_inside_and_linked_outputs_are_refused_without_overwrite(self):
        self.output.write_bytes(b"keep this output")
        with self.assertRaisesRegex(ValueError, "new .zip"):
            self.export()
        self.assertEqual(self.output.read_bytes(), b"keep this output")
        with self.assertRaisesRegex(ValueError, "outside"):
            self.export(self.repo / "ignored-output.zip")
        target = self.root / "actual"
        target.mkdir()
        link = self.root / "linked"
        try:
            link.symlink_to(target, target_is_directory=True)
        except OSError:
            return  # Windows without symlink privileges still checks other paths.
        with self.assertRaisesRegex(ValueError, "Linked path"):
            self.export(link / "new.zip")
        self.assertEqual(list(target.iterdir()), [])

    def test_source_and_manifest_tampering_is_detected(self):
        self.export()
        stage = self.extracted()
        original = (stage / "README.md").read_bytes()
        (stage / "README.md").write_text("changed after archive")
        with self.assertRaisesRegex(ValueError, "Changed source file"):
            package.verify_source_identity(stage)
        (stage / "README.md").write_bytes(original)
        (stage / "extra-evidence.txt").write_text("outside tracked-source identity")
        package.verify_source_identity(stage)
        with (stage / package.SOURCE_MANIFEST).open("a") as stream:
            stream.write("0" * 64 + "  extra-evidence.txt\n")
        with self.assertRaisesRegex(ValueError, "manifest"):
            package.verify_source_identity(stage)

    def test_path_admission_and_reserved_collisions(self):
        for name in ("../escape", "/absolute", "a/../b", "a\\b", "a:b", "a;b", "line\nfile", "a//b", ".git/config",
                     "CON", "aux.txt", "dir./file", "NUL.data", "a ", "./file", ""):
            with self.subTest(name=name), self.assertRaises(ValueError):
                package.safe_relative(name)
        for names in (("Readme", "README"), ("directory", "directory/file"), (package.IDENTITY,), ("SHA256SUMS.txt",)):
            with self.subTest(names=names), self.assertRaises(ValueError):
                package.checked_paths(names)
        self.assertEqual(package.checked_paths(["normal file.txt", "src/example.cpp"]), ["normal file.txt", "src/example.cpp"])

    def test_export_ignore_and_substituted_bytes_are_manifested(self):
        (self.repo / ".gitattributes").write_text("ignored.txt export-ignore\nversion.txt export-subst\n")
        (self.repo / "ignored.txt").write_text("intentionally omitted by Git archive")
        (self.repo / "version.txt").write_text("$Format:%H$\n")
        self.commit()
        self.run_git("tag", "-f", self.tag)
        self.export()
        stage = self.extracted()
        _, entries = package.verify_source_identity(stage)
        self.assertNotIn("ignored.txt", entries)
        self.assertEqual((stage / "version.txt").read_text().strip(), self.run_git("rev-parse", "HEAD"))
        self.assertEqual(entries["version.txt"], hashlib.sha256((stage / "version.txt").read_bytes()).hexdigest())

    def test_committed_symlinks_and_shallow_history_are_rejected(self):
        shallow = self.root / "shallow"
        self.run_git("clone", "--quiet", "--depth=1", self.repo.as_uri(), str(shallow))
        with self.assertRaisesRegex(ValueError, "Full history"):
            package.package_source(shallow, self.output, self.tag)
        blob = self.run_git("hash-object", "-w", "README.md")
        self.run_git("update-index", "--add", "--cacheinfo", "120000," + blob + ",link")
        self.run_git("commit", "--quiet", "-m", "symbolic link")
        self.run_git("checkout", "--", "link")
        self.run_git("tag", "-f", self.tag)
        with self.assertRaisesRegex(ValueError, "Linked files"):
            self.export()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
