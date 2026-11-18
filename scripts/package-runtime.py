#!/usr/bin/env python3
"""Build the audited Windows application payload and its portable ZIP.

This command does not execute Windows binaries. It reads the actual PE imports
of the two applications, Qt plugins and SDK sample, preserving only that runtime
closure. It never includes test executables or test plugin fixtures by glob.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile
import time
import zipfile

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("apo_stage_runtime", PROJECT / "scripts/stage_editor_runtime.py")
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)
SCHEMA = "apophysis-reborn.runtime-package"
GENERATED = {"package-manifest.json", "SHA256SUMS.txt"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def safe_relative(value):
    result = stage.safe_relative(value)
    # Win32 also recognizes the ISO-8859-1 superscript digits as device names.
    for component in value.split("/"):
        require(not re.fullmatch(r"(?:COM|LPT)[\u00b9\u00b2\u00b3]", component.split(".", 1)[0], re.I),
                "Reserved Windows device name: " + value)
    require(len(value.encode("utf-16-le")) // 2 <= 150,
            "Payload path is too long for the installer: " + value)
    return result


def ordinary_path(path):
    path = Path(path)
    for item in (path, *path.parents):
        require(not item.is_symlink(), "Symbolic-link input is not packaged: " + str(item))
        if item.exists():
            attrs = getattr(item.stat(follow_symlinks=False), "st_file_attributes", 0)
            require(not attrs & 0x400, "Reparse-point input is not packaged: " + str(item))
    return path


def regular(path):
    path = ordinary_path(path)
    require(path.is_file() and stat.S_ISREG(path.stat().st_mode), "Input is not a regular file: " + str(path))
    return path


def tree_files(root):
    """Walk ordinary directories only, rejecting links before descending."""
    root = ordinary_path(root)
    require(root.is_dir(), "Missing payload tree: " + str(root))
    pending = [root]
    while pending:
        directory = pending.pop()
        for path in sorted(directory.iterdir()):
            ordinary_path(path)
            if path.is_dir(): pending.append(path)
            else: yield regular(path)


def add_file(files, source, name):
    safe_relative(name)
    key = name.casefold()
    require(key not in {n.casefold() for n in GENERATED}, "Reserved generated payload path: " + name)
    for other in files:
        fold = other.casefold()
        require(fold != key and not fold.startswith(key + "/") and not key.startswith(fold + "/"),
                "Payload paths collide on Windows: " + name + " and " + other)
    files[name] = regular(source)


def add_tree(files, root, prefix):
    root = Path(root)
    for path in sorted(tree_files(root)):
        add_file(files, path, prefix + "/" + path.relative_to(root).as_posix())


def native_closure(runtime, objdump):
    runtime = Path(runtime).absolute()
    roots = ("apophysis-reborn.exe", "flame-cpu.exe", "platforms/qwindows.dll",
             "platforms/qoffscreen.dll", "styles/qmodernwindowsstyle.dll", "imageformats/qjpeg.dll",
             "plugins/reborn-wave/reborn_wave.dll")
    available = {}
    for path in tree_files(runtime):
        if path.suffix.lower() == ".dll" and "tests" not in path.relative_to(runtime).parts:
            key = path.name.casefold()
            require(key not in available, "Ambiguous staged DLL basename: " + path.name)
            available[key] = path
    visited, system = {}, set()

    def visit(path):
        path = regular(path)
        name = path.relative_to(runtime).as_posix()
        if name in visited:
            return
        imports, exports = stage.inspect(objdump, path)
        visited[name] = (path, imports, exports)
        for dependency in imports:
            if dependency in available:
                visit(available[dependency])
            elif dependency.startswith(("api-ms-win-", "ext-ms-win-")) or dependency in stage.WINDOWS_DLLS:
                system.add(dependency)
            else:
                raise ValueError("Missing runtime DLL " + dependency + " imported by " + name)

    for name in roots:
        visit(runtime / name)
    module_names = {Path(name).name.casefold(): row for name, row in visited.items()}
    resolved = 0
    for name, (_, imports, _) in visited.items():
        for dependency, symbols in imports.items():
            if dependency in module_names:
                missing = symbols - module_names[dependency][2]
                require(not missing, "Missing imported symbols in " + name + ": " + repr(sorted(missing)))
                resolved += len(symbols)
    require(not any(Path(name).name.casefold() == "vulkan-1.dll" for name in visited),
            "The system Vulkan loader must not be redistributed")
    return {name: row[0] for name, row in visited.items()}, {
        "non_system_imported_symbols_resolved": resolved,
        "windows_system_dependencies": sorted(system),
        "native_files": sorted(visited), "windows_executed": False,
    }


def check_lgpl_sources(root):
    root = Path(root)
    sums = regular(root / "SHA256SUMS.txt").read_text(encoding="utf-8")
    names = set()
    for line in sums.splitlines():
        expected, name = line.split("  ", 1)
        require(re.fullmatch(r"[0-9a-f]{64}", expected) is not None, "Invalid source archive checksum")
        safe_relative(name)
        require(name not in names and digest(regular(root / name)) == expected,
                "Duplicate or changed corresponding source archive: " + name)
        names.add(name)
    require(names and any("qt6-base" in name for name in names), "Qt corresponding sources are missing")
    actual = {p.relative_to(root).as_posix() for p in tree_files(root)}
    require(actual <= names | {"SHA256SUMS.txt", "README.txt"}, "Unlisted corresponding-source input")


def manifest_files(root):
    return {p.relative_to(root).as_posix(): {"sha256": digest(p), "bytes": p.stat().st_size}
            for p in sorted(tree_files(root))}


def verify_payload(root):
    root = Path(root).absolute()
    meta = json.loads(regular(root / "package-manifest.json").read_text(encoding="utf-8"))
    require(meta.get("schema") == SCHEMA and meta.get("schema_version") == 1, "Unknown package manifest")
    require(re.fullmatch(r"\d+\.\d+\.\d+", meta.get("version", "")) is not None, "Invalid package version")
    require(re.fullmatch(r"[0-9a-f]{40}", meta.get("source_commit", "")) is not None, "Invalid package commit")
    actual = manifest_files(root)
    expected = meta["files"]
    require(isinstance(expected, dict) and expected, "Empty or invalid payload file inventory")
    require(not GENERATED.intersection(expected), "Generated manifest/checksums cannot appear in the base inventory")
    require(set(actual) == set(expected) | GENERATED, "Payload file inventory differs from its manifest")
    folded = set()
    for name, record in expected.items():
        safe_relative(name)
        key = name.casefold()
        require(all(key != other and not key.startswith(other + "/") and not other.startswith(key + "/")
                    for other in folded | {n.casefold() for n in GENERATED}), "Case-insensitive manifest collision")
        folded.add(key)
        require(record == actual[name], "Payload file changed: " + name)
    sums = {}
    for line in (root / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
        value, name = line.split("  ", 1)
        safe_relative(name)
        require(name not in sums and re.fullmatch(r"[0-9a-f]{64}", value), "Invalid package checksum line")
        sums[name] = value
    require(set(sums) == set(actual) - {"SHA256SUMS.txt"}, "Checksum inventory differs")
    require(all(actual[name]["sha256"] == value for name, value in sums.items()), "Package checksum failed")
    return meta, actual


def portable_zip(payload, destination, epoch):
    # Stable metadata and ordering make the archive reproducible from equal bytes.
    moment = time.gmtime(max(315532800, min(int(epoch), 4354819198)))[:6]
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(payload.rglob("*")):
            if not path.is_file(): continue
            name = payload.name + "/" + path.relative_to(payload).as_posix()
            info = zipfile.ZipInfo(name, moment)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            with path.open("rb") as source, archive.open(info, "w", force_zip64=True) as target:
                shutil.copyfileobj(source, target, 1024 * 1024)
    with zipfile.ZipFile(destination) as archive:
        require(archive.testzip() is None, "Portable archive CRC failed")
        expected = manifest_files(payload)
        require(len(archive.infolist()) == len(expected), "Portable archive contains duplicate/missing entries")
        for name, record in expected.items():
            h = hashlib.sha256()
            with archive.open(payload.name + "/" + name) as source:
                for block in iter(lambda: source.read(1024 * 1024), b""): h.update(block)
            require(h.hexdigest() == record["sha256"], "Portable archive differs from payload: " + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", type=Path, help="Verify an existing payload without writing or executing files")
    parser.add_argument("--runtime", type=Path, help="Complete audited Windows staging directory")
    parser.add_argument("--output", type=Path, help="Directory for a new payload and portable ZIP")
    parser.add_argument("--lgpl-sources", type=Path, help="Corresponding archives with SHA256SUMS.txt")
    parser.add_argument("--objdump", help="PE-capable llvm-objdump/objdump executable")
    parser.add_argument("--source-commit", help="Exact 40-character source commit used to build the apps")
    parser.add_argument("--source-date-epoch", type=int, default=int(os.environ.get("SOURCE_DATE_EPOCH", "315532800")))
    args = parser.parse_args()
    if args.verify:
        require(not any((args.runtime, args.output, args.lgpl_sources, args.objdump, args.source_commit)),
                "Use --verify by itself")
        metadata, files = verify_payload(args.verify)
        print(json.dumps({"result": "PASS", "version": metadata["version"], "source_commit": metadata["source_commit"],
                          "verified_files": len(files), "windows_executed": False}, indent=2))
        return
    require(all((args.runtime, args.output, args.lgpl_sources, args.objdump, args.source_commit)),
            "Creation requires --runtime, --output, --lgpl-sources, --objdump and --source-commit")
    require(re.fullmatch(r"[0-9a-f]{40}", args.source_commit), "--source-commit must be the full Git commit")
    version = re.search(r"project\(apophysis_reborn VERSION ([0-9.]+)", (PROJECT / "CMakeLists.txt").read_text()).group(1)
    name = "apophysis-reborn-" + version + "-windows-x64"
    output = args.output.absolute()
    ordinary_path(output)
    require(not (output / name).exists() and not (output / (name + "-portable.zip")).exists(),
            "Payload/archive already exists; choose a new output directory")
    native, imports = native_closure(args.runtime, args.objdump)
    files = {}
    for relative, source in native.items(): add_file(files, source, relative)
    add_file(files, args.runtime / "qt.conf", "qt.conf")
    add_file(files, args.runtime / "plugins/reborn-wave/reborn_wave.apoplugin", "plugins/reborn-wave/reborn_wave.apoplugin")
    for folder in ("docs", "examples", "licenses", "preview"):
        add_tree(files, PROJECT / folder, folder)
    add_file(files, PROJECT / "packaging/PORTABLE_README.txt", "START-HERE.txt")
    add_file(files, PROJECT / "packaging/notices/NSIS-LICENSE.txt", "licenses/installer/NSIS-LICENSE.txt")
    add_file(files, PROJECT / "packaging/notices/nsis-provenance.json", "licenses/installer/nsis-provenance.json")
    # Keep notices referenced by the existing guides reachable in the runtime edition.
    for folder in ("third_party/lua", "third_party/vulkan"):
        for child in ("README.md", "README", "LICENSE", "provenance.json", "SHA256SUMS.txt"):
            path = PROJECT / folder / child
            if path.is_file(): add_file(files, path, folder + "/" + child)
    for child in ("ATTRIBUTION.md",):
        add_file(files, PROJECT / "tests/legacy" / child, "tests/legacy/" + child)
    add_file(files, PROJECT / "tests/reference-FLAM3-LICENSE.txt", "tests/reference-FLAM3-LICENSE.txt")
    check_lgpl_sources(args.lgpl_sources)
    add_tree(files, args.lgpl_sources, "third-party-sources")
    require(sum(path.stat().st_size for path in files.values()) < 2 * 1024 ** 3, "Runtime package exceeds 2 GiB")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".apo-runtime-", dir=output) as private:
        payload = Path(private) / name
        payload.mkdir()
        for relative, source in sorted(files.items()):
            destination = payload / safe_relative(relative)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
        metadata = {"schema": SCHEMA, "schema_version": 1, "version": version,
                    "source_commit": args.source_commit, "source_date_epoch": args.source_date_epoch,
                    "edition": "Windows x64 runtime", "runtime_audit": imports,
                    "files": manifest_files(payload)}
        (payload / "package-manifest.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
        entries = manifest_files(payload)
        (payload / "SHA256SUMS.txt").write_text("".join(record["sha256"] + "  " + relative + "\n"
            for relative, record in entries.items()), encoding="utf-8")
        verify_payload(payload)
        zipped = Path(private) / (name + "-portable.zip")
        portable_zip(payload, zipped, args.source_date_epoch)
        os.rename(payload, output / name)
        # Hard-link publication cannot replace an existing archive on either host.
        os.link(zipped, output / zipped.name)
        print(json.dumps({"payload": str(output / name), "portable_zip": str(output / zipped.name),
            "version": version, "source_commit": args.source_commit, "files": len(entries) + 1,
            "sha256": digest(zipped), "bytes": zipped.stat().st_size, "windows_executed": False}, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit("Runtime packaging failed: " + str(error))
