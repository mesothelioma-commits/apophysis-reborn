"""Stage a relocatable Windows Qt Widgets build and verify its DLL imports."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

WINDOWS_DLLS = {
    "advapi32.dll", "authz.dll", "bcrypt.dll", "comctl32.dll", "comdlg32.dll",
    "crypt32.dll", "d3d9.dll", "d3d11.dll", "d3d12.dll", "dwrite.dll", "dwmapi.dll",
    "dxgi.dll", "gdi32.dll", "imm32.dll", "iphlpapi.dll", "kernel32.dll", "mpr.dll",
    "netapi32.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll", "opengl32.dll",
    "powrprof.dll", "rpcrt4.dll", "secur32.dll", "setupapi.dll", "shcore.dll",
    "shell32.dll", "shlwapi.dll", "user32.dll", "userenv.dll", "usp10.dll",
    "uxtheme.dll", "version.dll", "winmm.dll", "winspool.drv", "ws2_32.dll", "wtsapi32.dll",
}


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
    records = {}
    system = set()

    def visit(path, relative):
        name = path.name.lower()
        if name in records:
            return
        imports, exports = inspect(args.objdump, path)
        records[name] = (path, relative, imports, exports)
        for dependency in imports:
            if dependency in candidates:
                visit(candidates[dependency], Path(candidates[dependency].name))
            elif dependency.startswith(("api-ms-win-", "ext-ms-win-")) or dependency in WINDOWS_DLLS:
                system.add(dependency)
            else:
                raise RuntimeError(f"Missing runtime dependency {dependency}, imported by {path.name}")

    for path, relative in roots:
        visit(path, relative)
    symbols = 0
    for path, _, imports, _ in records.values():
        for dependency, names in imports.items():
            if dependency in records:
                missing = names - records[dependency][3]
                if missing:
                    raise RuntimeError(f"{path.name} imports unavailable symbols from {dependency}: {sorted(missing)}")
                symbols += len(names)
    output.mkdir(parents=True, exist_ok=True)
    for path, relative, _, _ in records.values():
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
    (output / "qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=.\n", encoding="utf-8")
    manifest = {
        "non_system_imported_symbols_resolved": symbols,
        "files": sorted(str(row[1]) for row in records.values()),
        "windows_system_dependencies": sorted(system),
    }
    (output / "runtime-dependencies.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Staged {len(records)} executables, DLLs and plugins. Resolved {symbols} non-system imported symbols.")


if __name__ == "__main__":
    main()
