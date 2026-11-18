"""Exercise the real configure-time source identity in compiler-free fixtures."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CMAKE = os.environ.get("APO_TEST_CMAKE", "cmake")
VERSION = re.search(r"project\(apophysis_reborn VERSION ([0-9.]+)",
                    (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)


class BuildIdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cmake = shutil.which(CMAKE)
        if not cls.cmake:
            raise RuntimeError("CMake is required; pass --cmake PATH or set APO_TEST_CMAKE")
        cls.git = shutil.which("git")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="apo identity ")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.source = self.work / "source with spaces"
        self.build = self.work / "build with spaces"
        (self.source / "cmake").mkdir(parents=True)
        (self.source / "src").mkdir()
        for name in ("BuildIdentity.cmake", "apo_build_config.hpp.in"):
            shutil.copyfile(ROOT / "cmake" / name, self.source / "cmake" / name)
        (self.source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\n"
            f"project(identity_fixture VERSION {VERSION} LANGUAGES NONE)\n"
            "set(APO_BUILD_EDITOR OFF)\n"
            "include(cmake/BuildIdentity.cmake)\n", encoding="utf-8")
        (self.source / "src/build_info.cpp").write_text("// source fixture\n", encoding="utf-8")
        self.env = dict(os.environ)
        for name in tuple(self.env):
            if name.startswith("GIT_") or name in ("CMAKE_GENERATOR", "CMAKE_GENERATOR_PLATFORM",
                                                   "CMAKE_GENERATOR_TOOLSET"):
                self.env.pop(name)
        self.env["GIT_CONFIG_NOSYSTEM"] = "1"
        self.env["GIT_CONFIG_GLOBAL"] = str(self.work / "empty-global-git-config")
        (self.work / "empty-global-git-config").write_text("", encoding="utf-8")
        self.generator = os.environ.get("APO_TEST_CMAKE_GENERATOR")
        if not self.generator and shutil.which("ninja"):
            self.generator = "Ninja"
        elif not self.generator and shutil.which("make"):
            self.generator = "Unix Makefiles"

    def run_command(self, argv, *, success=True):
        result = subprocess.run([str(value) for value in argv], cwd=self.work, env=self.env,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=30, check=False)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def configure(self, *, success=True):
        command = [self.cmake, "-S", self.source, "-B", self.build]
        if self.generator:
            command += ["-G", self.generator]
        return self.run_command(command, success=success)

    def identity(self):
        header = (self.build / "generated/apo_build_config.hpp").read_text(encoding="utf-8")
        values = {}
        for key, value in re.findall(r"^#define (APO_[A-Z_]+) (.+)$", header, re.MULTILINE):
            values[key] = value.strip().strip('"')
        return {"revision": values["APO_SOURCE_REVISION"],
                "origin": values["APO_SOURCE_ORIGIN"],
                "known": int(values["APO_SOURCE_DIRTY_KNOWN"]),
                "dirty": int(values["APO_SOURCE_DIRTY"])}

    def stamp(self, *, manifest=True, dirty=False, extra_lines=(), **changes):
        metadata = {"schema_version": 1, "version": VERSION,
                    "source_revision": "0123456789abcdef" * 2 + "01234567", "source_dirty": dirty}
        if manifest:
            paths = sorted(path for path in self.source.rglob("*")
                           if path.is_file() and path.name not in ("release-source.json", "SOURCE_SHA256SUMS.txt"))
            lines = [hashlib.sha256(path.read_bytes()).hexdigest() + "  " +
                     path.relative_to(self.source).as_posix() for path in paths]
            lines.extend(extra_lines)
            data = ("\n".join(lines) + "\n").encode("utf-8")
            (self.source / "SOURCE_SHA256SUMS.txt").write_bytes(data)
            metadata.update(source_manifest="SOURCE_SHA256SUMS.txt",
                            source_manifest_sha256=hashlib.sha256(data).hexdigest())
        metadata.update(changes)
        (self.source / "release-source.json").write_text(json.dumps(metadata) + "\n", encoding="utf-8")
        return metadata

    def test_raw_export_reports_unknown_identity(self):
        self.configure()
        self.assertEqual(self.identity(), dict(revision="unknown", origin="unknown", known=0, dirty=0))

    def test_stamp_without_manifest_does_not_claim_clean_source(self):
        metadata = self.stamp(manifest=False)
        self.configure()
        self.assertEqual(self.identity(), dict(revision=metadata["source_revision"],
                                               origin="release-source.json", known=0, dirty=0))

    def test_hash_bound_manifest_establishes_clean_source(self):
        metadata = self.stamp()
        self.configure()
        self.assertEqual(self.identity(), dict(revision=metadata["source_revision"],
                                               origin="release-source.json+manifest", known=1, dirty=0))

    def test_changed_source_is_known_dirty(self):
        self.stamp()
        (self.source / "src/build_info.cpp").write_text("// edited after export\n", encoding="utf-8")
        self.configure()
        self.assertEqual((self.identity()["known"], self.identity()["dirty"]), (1, 1))

    def test_missing_source_is_known_dirty(self):
        self.stamp()
        (self.source / "src/build_info.cpp").unlink()
        self.configure()
        self.assertEqual((self.identity()["known"], self.identity()["dirty"]), (1, 1))

    def test_declared_dirty_source_stays_dirty_even_with_matching_manifest(self):
        self.stamp(dirty=True)
        self.configure()
        self.assertEqual((self.identity()["known"], self.identity()["dirty"]), (1, 1))

    def test_manifest_tampering_is_rejected(self):
        self.stamp()
        with (self.source / "SOURCE_SHA256SUMS.txt").open("a", encoding="utf-8") as output:
            output.write("0" * 64 + "  altered.cpp\n")
        result = self.configure(success=False)
        self.assertIn("manifest differs", result.stdout)

    def test_missing_named_manifest_is_rejected(self):
        self.stamp()
        (self.source / "SOURCE_SHA256SUMS.txt").unlink()
        self.assertIn("manifest is missing", self.configure(success=False).stdout)

    def test_traversal_and_duplicate_manifest_paths_are_rejected(self):
        for path in ("../outside.cpp", "src/../../outside.cpp", "src\\outside.cpp",
                     "/outside.cpp", "C:/outside.cpp", "CMakeLists.txt"):
            with self.subTest(path=path):
                self.stamp(extra_lines=["0" * 64 + "  " + path])
                self.assertIn("Invalid or duplicate path", self.configure(success=False).stdout)

    def test_stamp_cannot_redirect_the_manifest(self):
        self.stamp(source_manifest="../SOURCE_SHA256SUMS.txt")
        self.assertIn("unsupported name", self.configure(success=False).stdout)

    def test_incompatible_or_malformed_metadata_is_rejected(self):
        for changes in ({"version": "99.99.99"}, {"schema_version": 2},
                        {"source_dirty": "false"}, {"source_revision": "not-a-full-commit"},
                        {"source_revision": "unknown"}):
            with self.subTest(changes=changes):
                self.stamp(manifest=False, **changes)
                self.configure(success=False)

    def git_command(self, *arguments):
        if not self.git:
            self.skipTest("Git is not available for Git source-identity tests")
        return self.run_command([self.git, "-C", self.source, *arguments]).stdout.strip()

    def init_git(self):
        self.git_command("init", "-q")
        self.git_command("config", "user.name", "Identity fixture")
        self.git_command("config", "user.email", "fixture@example.invalid")
        self.git_command("config", "core.autocrlf", "false")
        self.git_command("config", "commit.gpgsign", "false")
        self.git_command("add", ".")
        self.git_command("commit", "-qm", "initial identity fixture")
        return self.git_command("rev-parse", "HEAD")

    def build_after_change(self):
        self.run_command([self.cmake, "--build", self.build, "--parallel", "1"])

    def test_git_revision_and_clean_state_are_recorded(self):
        revision = self.init_git()
        self.configure()
        self.assertEqual(self.identity(), dict(revision=revision, origin="git", known=1, dirty=0))

    def test_git_source_edit_and_commit_refresh_identity_on_build(self):
        first = self.init_git()
        self.configure()
        source = self.source / "src/build_info.cpp"
        source.write_text("// changed source tracked by configure dependencies\n", encoding="utf-8")
        self.build_after_change()
        self.assertEqual(self.identity(), dict(revision=first, origin="git", known=1, dirty=1))
        self.git_command("add", "src/build_info.cpp")
        self.git_command("commit", "-qm", "second identity fixture")
        second = self.git_command("rev-parse", "HEAD")
        self.assertNotEqual(first, second)
        self.build_after_change()
        self.assertEqual(self.identity(), dict(revision=second, origin="git", known=1, dirty=0))

    def test_untracked_shipped_source_refreshes_dirty_state_on_build(self):
        self.init_git()
        self.configure()
        (self.source / "docs").mkdir()
        (self.source / "docs/new-guide.md").write_text("new untracked guide\n", encoding="utf-8")
        self.build_after_change()
        self.assertEqual((self.identity()["known"], self.identity()["dirty"]), (1, 1))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--cmake", default=CMAKE)
    arguments, remaining = parser.parse_known_args()
    CMAKE = arguments.cmake
    unittest.main(argv=[sys.argv[0], *remaining])
