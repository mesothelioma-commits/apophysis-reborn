#!/usr/bin/env python3
"""Package a clean tagged checkout, complete reachable history and source identity.

Requires Python 3.9+ and Git. No build tools or third-party Python modules are
needed. The archive has deterministic member ordering/timestamps and contains no
machine-specific paths. Equal source/ref inputs with the same Git/Python/zlib
versions produce equal bytes; compressed bytes across tool versions may differ.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
import time
import zipfile

PREFIX = "apophysis-reborn"
SOURCE_MANIFEST = "SOURCE_SHA256SUMS.txt"
IDENTITY = "release-source.json"
BUNDLE = "apophysis-reborn.bundle"
GENERATED = {SOURCE_MANIFEST, IDENTITY, BUNDLE, "SHA256SUMS.txt", "source-package.json", "SOURCE_PACKAGE.txt"}
MAX_MANIFEST = 1024 * 1024
WINDOWS_RESERVED = {"CON", "PRN", "AUX", "NUL", *("COM" + str(n) for n in range(1, 10)),
                    *("LPT" + str(n) for n in range(1, 10))}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def git(repo, *args, binary=False):
    result = subprocess.run(["git", "-C", str(repo), *args], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False)
    require(result.returncode == 0, "Git failed: " + result.stderr.decode("utf-8", errors="replace").strip())
    return result.stdout if binary else result.stdout.decode("utf-8").strip()


def no_links(path):
    """Check the lexical path before resolve() can hide a link or junction."""
    path = Path(path).absolute()
    for item in (path, *path.parents):
        require(not item.is_symlink(), "Linked path is not accepted: " + str(item))
        if item.exists():
            attrs = getattr(os.stat(item, follow_symlinks=False), "st_file_attributes", 0)
            require(not attrs & 0x400, "Reparse-point path is not accepted: " + str(item))
    return path


def safe_relative(name):
    require(isinstance(name, str) and name and not any(ord(c) < 32 or ord(c) == 127 for c in name),
            "Invalid source path")
    path = PurePosixPath(name)
    require(not path.is_absolute() and path.as_posix() == name and
            not any(c in name for c in '\\:;<>"|?*') and
            all(part not in {".", ".."} and not part.endswith((".", " ")) and
                part.casefold() != ".git" and part.split(".", 1)[0].upper() not in WINDOWS_RESERVED
                for part in path.parts), "Unsafe or nonportable source path: " + name)
    return path


def checked_paths(names, reserved=GENERATED):
    result, folded = [], set()
    reserved_folded = {name.casefold() for name in reserved}
    for name in names:
        safe_relative(name)
        key = name.casefold()
        require(key not in reserved_folded and key not in folded,
                "Duplicate, case-colliding or reserved source path: " + name)
        require(not any(key.startswith(other + "/") or other.startswith(key + "/") for other in folded),
                "Source file/directory collision: " + name)
        folded.add(key)
        result.append(name)
    return sorted(result)


def clean_revision(repo, tag):
    repo = no_links(repo).resolve()
    require(repo.is_dir() and Path(git(repo, "rev-parse", "--show-toplevel")).resolve() == repo,
            "--repo must name the Git working-tree root")
    require(git(repo, "rev-parse", "--is-shallow-repository") == "false",
            "Full history is required; fetch the missing history before packaging")
    require(not git(repo, "status", "--porcelain", "--untracked-files=all"),
            "The checkout must be clean, including untracked files")
    revision = git(repo, "rev-parse", "HEAD")
    require(re.fullmatch(r"[0-9a-f]{40}", revision), "Expected a full SHA-1 source revision")
    git(repo, "check-ref-format", "refs/tags/" + tag)
    require(git(repo, "rev-parse", "refs/tags/" + tag + "^{commit}") == revision,
            "The requested release tag does not identify HEAD")
    return repo, revision


def source_version(repo, revision):
    content = git(repo, "show", revision + ":CMakeLists.txt")
    match = re.search(r"\bproject\s*\(\s*apophysis_reborn\s+VERSION\s+(\d+\.\d+\.\d+)\b", content, re.I)
    require(match is not None, "Cannot read the project version from committed CMakeLists.txt")
    return match.group(1)


def archive_git_source(repo, revision, destination):
    """Export regular Git archive files, rejecting unsafe archive members.

    This intentionally respects Git export-ignore/export-subst attributes.
    Symlinks and gitlinks are rejected instead of dereferencing outside inputs.
    The returned names are exactly the regular files included by git archive.
    """
    destination = no_links(destination)
    require(destination.is_dir() and not any(destination.iterdir()), "Source export requires an empty directory")
    listing = git(repo, "ls-tree", "-r", "-z", revision, binary=True)
    tree_paths = []
    for record in listing.split(b"\0"):
        if not record:
            continue
        metadata, raw_name = record.split(b"\t", 1)
        mode, kind, _ = metadata.split(b" ", 2)
        require(mode in {b"100644", b"100755"} and kind == b"blob",
                "Linked files and submodules require a separate explicit packaging policy")
        tree_paths.append(raw_name.decode("utf-8"))
    checked_paths(tree_paths)
    with tempfile.TemporaryFile() as archive_file:
        result = subprocess.run(["git", "-C", str(repo), "archive", "--format=tar", revision],
                                stdout=archive_file, stderr=subprocess.PIPE, check=False)
        require(result.returncode == 0, "git archive failed: " + result.stderr.decode("utf-8", errors="replace"))
        archive_file.seek(0)
        with tarfile.open(fileobj=archive_file, mode="r:") as archive:
            members = archive.getmembers()
            files = [member for member in members if member.isfile()]
            names = checked_paths(member.name for member in files)
            require(set(names) <= set(tree_paths), "Git archive produced an unexpected source file")
            for member in members:
                if member.isdir():
                    safe_relative(member.name.rstrip("/"))
                else:
                    require(member.isfile(), "Unsupported Git archive member: " + member.name)
            for member in files:
                target = destination.joinpath(*PurePosixPath(member.name).parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as source, target.open("xb") as output:
                    shutil.copyfileobj(source, output, 1024 * 1024)
                target.chmod(0o755 if member.mode & 0o111 else 0o644)
    require(names, "The source archive is empty")
    return names


def write_source_identity(directory, source_paths, version, revision):
    """Write the shared source-identity contract, excluding distribution extras."""
    directory = no_links(directory)
    require(re.fullmatch(r"\d+\.\d+\.\d+", version), "Invalid source version")
    require(re.fullmatch(r"[0-9a-f]{40}", revision), "Invalid source revision")
    names = checked_paths(source_paths)
    require({"CMakeLists.txt", "src/build_info.cpp"} <= set(names), "Source identity omits required project inputs")
    lines = []
    for name in names:
        path = no_links(directory / name)
        require(path.is_file() and stat.S_ISREG(path.stat().st_mode), "Missing regular source file: " + name)
        lines.append(sha256(path) + "  " + name + "\n")
    manifest = "".join(lines).encode("utf-8")
    require(0 < len(manifest) <= MAX_MANIFEST, "Source manifest exceeds the supported 1 MiB limit")
    with (directory / SOURCE_MANIFEST).open("xb") as output:
        output.write(manifest)
    identity = {"schema_version": 1, "version": version, "source_revision": revision,
                "source_dirty": False, "source_manifest": SOURCE_MANIFEST,
                "source_manifest_sha256": hashlib.sha256(manifest).hexdigest()}
    with (directory / IDENTITY).open("x", encoding="utf-8", newline="\n") as output:
        json.dump(identity, output, indent=2, sort_keys=True)
        output.write("\n")
    return identity


def verify_source_identity(directory):
    directory = no_links(directory)
    identity_path = no_links(directory / IDENTITY)
    require(identity_path.stat().st_size <= 4096, "Source identity metadata exceeds its size limit")
    identity = json.loads(identity_path.read_text(encoding="utf-8"))
    require(type(identity.get("schema_version")) is int and identity["schema_version"] == 1 and
            identity.get("source_dirty") is False and identity.get("source_manifest") == SOURCE_MANIFEST and
            re.fullmatch(r"[0-9a-f]{40}", identity.get("source_revision", "")) and
            re.fullmatch(r"\d+\.\d+\.\d+", identity.get("version", "")), "Invalid source identity metadata")
    manifest = no_links(directory / SOURCE_MANIFEST)
    require(0 < manifest.stat().st_size <= MAX_MANIFEST and sha256(manifest) == identity.get("source_manifest_sha256"),
            "Source manifest is missing, oversized or changed")
    entries = {}
    for line in manifest.read_text(encoding="utf-8").splitlines():
        require("  " in line, "Invalid source manifest line")
        digest, name = line.split("  ", 1)
        require(re.fullmatch(r"[0-9a-f]{64}", digest) and name not in entries, "Invalid or duplicate source checksum")
        entries[name] = digest
    checked_paths(entries)
    require({"CMakeLists.txt", "src/build_info.cpp"} <= set(entries), "Source manifest omits required inputs")
    for name, expected in entries.items():
        path = no_links(directory / name)
        require(path.is_file() and sha256(path) == expected, "Changed source file: " + name)
    return identity, entries


def make_zip(directory, destination, epoch):
    files = sorted(path for path in directory.rglob("*") if path.is_file())
    checksum_path = directory / "SHA256SUMS.txt"
    with checksum_path.open("x", encoding="utf-8", newline="\n") as output:
        output.write("".join(sha256(path) + "  " + path.relative_to(directory).as_posix() + "\n" for path in files))
    files.append(checksum_path)
    moment = time.gmtime(max(315532800, min(int(epoch), 4354819198)))[:6]
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9, allowZip64=True) as archive:
        for path in sorted(files):
            info = zipfile.ZipInfo(PREFIX + "/" + path.relative_to(directory).as_posix(), moment)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (0o100755 if path.stat().st_mode & 0o111 else 0o100644) << 16
            with path.open("rb") as source, archive.open(info, "w", force_zip64=True) as output:
                shutil.copyfileobj(source, output, 1024 * 1024)
    with zipfile.ZipFile(destination) as archive:
        require(archive.testzip() is None, "Source archive failed CRC verification")
        require(len(archive.infolist()) == len(files), "Source archive member count differs")
        for path in files:
            with archive.open(PREFIX + "/" + path.relative_to(directory).as_posix()) as source:
                value = hashlib.sha256()
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    value.update(block)
            require(value.hexdigest() == sha256(path), "Source archive changed a packaged file")


def package_source(repo, output, tag, expected_version=None):
    repo, revision = clean_revision(repo, tag)
    version = source_version(repo, revision)
    require(expected_version is None or version == expected_version, "The committed version differs from --version")
    output = no_links(output)
    require(output.suffix.casefold() == ".zip" and not output.exists(), "Output must be a new .zip file")
    require(not output.resolve().is_relative_to(repo), "Place the output outside the source working tree")
    output.parent.mkdir(parents=True, exist_ok=True)
    no_links(output.parent)
    refs = git(repo, "for-each-ref", "--format=%(refname)", "refs/heads", "refs/tags").splitlines()
    require(refs, "The repository has no local branch or tag references")
    expected_refs = {name: git(repo, "rev-parse", name) for name in refs}
    epoch = int(git(repo, "show", "-s", "--format=%ct", revision))
    with tempfile.TemporaryDirectory(prefix="apo-source-package-", dir=output.parent) as temporary:
        temporary = Path(temporary)
        stage = temporary / PREFIX
        stage.mkdir()
        names = archive_git_source(repo, revision, stage)
        identity = write_source_identity(stage, names, version, revision)
        git(repo, "-c", "pack.threads=1", "-c", "pack.useBitmaps=false", "bundle", "create", str(stage / BUNDLE),
            "HEAD", *refs)
        git(repo, "bundle", "verify", str(stage / BUNDLE))
        metadata = {"schema": "apophysis-reborn.source-package", "schema_version": 1,
                    "version": version, "source_revision": revision, "release_tag": tag,
                    "source_date_epoch": epoch, "source_file_count": len(names),
                    "source_manifest_sha256": identity["source_manifest_sha256"],
                    "bundle": BUNDLE, "bundle_sha256": sha256(stage / BUNDLE), "refs": expected_refs,
                    "history_scope": "Complete history reachable from HEAD, all local branches and all tags",
                    "reproducibility_scope": "Identical inputs and Git/Python/zlib tool versions"}
        (stage / "source-package.json").write_bytes((json.dumps(metadata, indent=2, sort_keys=True) + "\n").encode("utf-8"))
        (stage / "SOURCE_PACKAGE.txt").write_bytes((
            "Apophysis Reborn " + version + " source archive\n\n"
            "Release tag: " + tag + "\nSource revision: " + revision + "\n\n"
            "This archive contains source and full reachable Git history. It contains no compiled applications.\n"
            "SOURCE_SHA256SUMS.txt covers exported Git source files only. release-source.json binds that manifest\n"
            "to the source revision. SHA256SUMS.txt covers every packaged file except itself.\n"
            "CMake reports source cleanliness at configuration time; distribution extras are outside that scope.\n\n"
            "Restore history from this directory:\n  git clone apophysis-reborn.bundle restored-source\n"
            "  git -C restored-source checkout " + tag + "\n\n"
            "Follow README.md and docs/RELEASE_HANDOFF.md for dependencies, building and release checks.\n"
            "Recreate this archive from the clean checkout:\n"
            "  python3 scripts/package-source.py --repo . --tag " + tag + " --output ../source.zip\n"
        ).encode("utf-8"))
        verify_source_identity(stage)
        archive = temporary / "source.zip"
        make_zip(stage, archive, epoch)
        # Recheck before publishing: source/tag/refs must not change during export.
        require(clean_revision(repo, tag)[1] == revision and
                {name: git(repo, "rev-parse", name) for name in refs} == expected_refs and
                git(repo, "for-each-ref", "--format=%(refname)", "refs/heads", "refs/tags").splitlines() == refs,
                "Source or reference inputs changed during packaging")
        no_links(output.parent)
        # A same-filesystem hard link publishes atomically and refuses collisions.
        # No replace/overwrite fallback is used on unsupported filesystems.
        os.link(archive, output)
    return {"version": version, "source_revision": revision, "release_tag": tag,
            "source_files": len(names), "refs": len(refs), "bytes": output.stat().st_size,
            "sha256": sha256(output)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1], help="Clean Git working-tree root")
    parser.add_argument("--output", type=Path, required=True, help="New ZIP path outside the working tree; never overwritten")
    parser.add_argument("--tag", required=True, help="Existing release tag identifying HEAD")
    parser.add_argument("--version", help="Optional assertion of the version in committed CMakeLists.txt")
    args = parser.parse_args(argv)
    try:
        result = package_source(args.repo, args.output, args.tag, args.version)
    except (ValueError, OSError, subprocess.SubprocessError, UnicodeError, tarfile.TarError, zipfile.BadZipFile) as exc:
        parser.exit(1, "Source packaging failed: " + str(exc) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
