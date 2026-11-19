"""Stage a relocatable Windows Qt Widgets build and verify its DLL imports."""
import argparse
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess

WINDOWS_DLLS = {
    "advapi32.dll", "authz.dll", "bcrypt.dll", "comctl32.dll", "comdlg32.dll",
    "crypt32.dll", "d3d9.dll", "d3d11.dll", "d3d12.dll", "dwrite.dll", "dwmapi.dll",
    "dnsapi.dll", "dxgi.dll", "gdi32.dll", "imm32.dll", "iphlpapi.dll", "kernel32.dll", "mpr.dll",
    "netapi32.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll", "opengl32.dll",
    "powrprof.dll", "rpcrt4.dll", "secur32.dll", "setupapi.dll", "shcore.dll",
    "shell32.dll", "shlwapi.dll", "user32.dll", "userenv.dll", "usp10.dll",
    "uxtheme.dll", "version.dll", "winhttp.dll", "winmm.dll", "winspool.drv", "ws2_32.dll", "wtsapi32.dll",
}


def safe_relative(value):
    """A portable, Windows-safe filename below the staging directory."""
    if not value or value.startswith("/") or "\\" in value or ":" in value:
        raise ValueError(f"Staging destination must be a relative path using '/': {value!r}")
    parts = value.split("/")
    reserved = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}
    reserved.update(f"{prefix}{number}" for prefix in ("COM", "LPT") for number in range(1, 10))
    for part in parts:
        if (not part or part in (".", "..") or part.endswith((".", " ")) or
                any(ord(c) < 32 or c in '<>"|?*' for c in part) or
                part.split(".", 1)[0].upper() in reserved):
            raise ValueError(f"Unsupported staging path component in {value!r}")
    return Path(*PurePosixPath(value).parts)


def destination_path(output, relative):
    destination = output / relative
    # Reject symlink destinations/parents before writing: a relative spelling
    # alone must not let an old staging directory redirect a copy elsewhere.
    for candidate in (destination, *destination.parents):
        if candidate == output:
            break
        if candidate.is_symlink():
            raise ValueError(f"Staging destination traverses a symlink: {destination}")
    try:
        destination.resolve().relative_to(output)
    except ValueError:
        raise ValueError(f"Staging destination escapes the output directory: {destination}")
    if destination.exists() and not destination.is_file():
        raise ValueError(f"Staging destination is not a regular file: {destination}")
    return destination


def inspect(objdump, path):
    text = subprocess.check_output([str(objdump), "-p", str(path)], text=True)
    imports = {}
    current = None
    for line in text.split("Export Table:")[0].splitlines():
        match = re.search(r"DLL Name: (.*)", line)
        if match:
            current = match[1].lower()
            imports[current] = set()
            continue
        match = re.fullmatch(r"\s+\d+\s+(\S+)", line)
        if match and current:
            imports[current].add(match[1])
    export_text = text.split("Export Table:")[-1] if "Export Table:" in text else ""
    exports = set(re.findall(r"^\s+\d+\s+0x[0-9a-f]+\s+(\S+)", export_text, re.M))
    return imports, exports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-prefix", required=True, type=Path)
    parser.add_argument("--compiler-runtime", required=True, type=Path)
    parser.add_argument("--objdump", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--module", nargs=2, action="append", default=[],
                        metavar=("SOURCE", "RELATIVE"),
                        help="Add a PE executable/DLL root at RELATIVE and resolve its imports; repeatable")
    parser.add_argument("--data", nargs=2, action="append", default=[],
                        metavar=("SOURCE", "RELATIVE"),
                        help="Copy a data file such as a plugin manifest to RELATIVE; repeatable")
    parser.add_argument("executables", nargs="+", type=Path)
    args = parser.parse_args()
    qt = args.qt_prefix.resolve()
    output = args.output.resolve()
    candidates = {p.name.lower(): p for p in args.compiler_runtime.glob("*.dll")}
    # Qt's MSYS2 dependencies use its libc++ unwind exports. Prefer this runtime
    # when the compiler distribution also includes a DLL with the same name.
    candidates.update({p.name.lower(): p for p in (qt / "bin").glob("*.dll")})
    plugins = qt / "share/qt6/plugins"
    if not plugins.is_dir():
        plugins = qt / "plugins"
    required_plugins = ["platforms/qwindows.dll", "platforms/qoffscreen.dll",
                        "styles/qmodernwindowsstyle.dll", "imageformats/qjpeg.dll"]
    roots = [(p, Path(p.name)) for p in args.executables]
    roots += [(plugins / p, Path(p)) for p in required_plugins]
    roots += [(Path(source), safe_relative(relative)) for source, relative in args.module]
    data = [(Path(source), safe_relative(relative)) for source, relative in args.data]
    for source, _ in roots + data:
        if not source.is_file():
            raise FileNotFoundError(f"Staging source is not a regular file: {source}")
    records = {}
    placements = {}
    claimed = {"qt.conf": None, "runtime-dependencies.json": None}
    system = set()

    def place(source, relative):
        relative = safe_relative(relative.as_posix())
        key = relative.as_posix().casefold()
        canonical = source.resolve()
        if key in claimed and claimed[key] != canonical:
            raise ValueError(f"Two staging inputs claim destination {relative}")
        for other in claimed:
            if key.startswith(other + "/") or other.startswith(key + "/"):
                raise ValueError(f"Staging file/directory paths conflict: {relative} and {other}")
        claimed[key] = canonical
        placements[key] = (source, relative)

    # App/Qt/compiler DLL names remain unambiguous across the package. Module
    # dependencies adjacent to their source can also be staged, at app-root
    # level where LoadLibraryEx's default application search finds them.
    root_dlls = {}
    for source, _ in roots:
        if source.suffix.lower() == ".dll":
            key = source.name.lower()
            if key in root_dlls and root_dlls[key].resolve() != source.resolve():
                raise ValueError(f"Ambiguous native module basename: {source.name}")
            root_dlls[key] = source

    def dependency_source(importer, name):
        if name in candidates:
            return candidates[name]
        adjacent = [p for p in importer.parent.iterdir()
                    if p.is_file() and p.name.lower() == name]
        if len(adjacent) > 1:
            raise ValueError(f"Case-insensitive DLL collision beside {importer}: {name}")
        if adjacent:
            return adjacent[0]
        return root_dlls.get(name)

    def visit(path, relative):
        place(path, relative)
        name = path.name.lower()
        if name in records:
            if records[name][0].resolve() != path.resolve():
                raise ValueError(f"Two different native files have the same DLL/executable basename: {name}")
            return
        imports, exports = inspect(args.objdump, path)
        records[name] = (path, relative, imports, exports)
        for dependency in imports:
            resolved = candidates.get(dependency)
            if resolved is None and (dependency.startswith(("api-ms-win-", "ext-ms-win-")) or
                                     dependency in WINDOWS_DLLS):
                system.add(dependency)
                continue
            if resolved is None:
                resolved = dependency_source(path, dependency)
            if resolved is not None:
                visit(resolved, Path(resolved.name))
            else:
                raise RuntimeError(f"Missing runtime dependency {dependency}, imported by {path.name}")

    for path, relative in roots:
        visit(path, relative)
    for source, relative in data:
        if relative.suffix.lower() in (".exe", ".dll"):
            raise ValueError(f"Use --module for executable/DLL payloads so imports are verified: {relative}")
        place(source, relative)
    symbols = 0
    for path, _, imports, _ in records.values():
        for dependency, names in imports.items():
            if dependency in records:
                missing = names - records[dependency][3]
                if missing:
                    raise RuntimeError(f"{path.name} imports unavailable symbols from {dependency}: {sorted(missing)}")
                symbols += len(names)
    # Complete path and dependency validation before copying any payload.
    copies = [(source, destination_path(output, relative)) for source, relative in placements.values()]
    destination_path(output, Path("qt.conf"))
    destination_path(output, Path("runtime-dependencies.json"))
    output.mkdir(parents=True, exist_ok=True)
    for path, destination in copies:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
    (output / "qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=.\n", encoding="utf-8")
    manifest = {
        "non_system_imported_symbols_resolved": symbols,
        "files": sorted(row[1].as_posix() for row in placements.values()),
        "data_files": sorted(relative.as_posix() for _, relative in data),
        "windows_system_dependencies": sorted(system),
    }
    (output / "runtime-dependencies.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Staged {len(placements)} files ({len(records)} native binaries, {len(data)} data files). "
          f"Resolved {symbols} non-system imported symbols.")


if __name__ == "__main__":
    main()
