#!/usr/bin/env python3
"""Compile the NSIS installer from an already verified runtime payload."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("apo_runtime_package", ROOT / "scripts/package-runtime.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


def nsis_string(value):
    text = str(value)
    package.require(not any(c in text for c in "\r\n\0"), "Newlines/NUL are not valid installer paths")
    # First survive the compile-time ${...}/$%...% expansion, then produce the
    # doubled dollars required by runtime strings. Plain $$ alone does not stop
    # the NSIS preprocessor from expanding an environment value in a filename.
    return text.replace("$", "${U+24}${U+24}").replace('"', '$\\"')


def cpp_wide(value):
    result = 'L"'
    for c in value:
        if c == "\\": result += "\\\\"
        elif c == '"': result += '\\"'
        elif 32 <= ord(c) <= 126: result += c
        elif ord(c) <= 0xFFFF: result += "\\u%04x" % ord(c)
        else: result += "\\U%08x" % ord(c)
    return result + '"'


def generate_header(payload, metadata, files):
    lines = ["#pragma once", "#include <cstdint>",
             "struct PayloadEntry { const wchar_t *path; const char *hash; std::uint64_t bytes; };",
             "inline constexpr auto payload_version = " + cpp_wide(metadata["version"]) + ";",
             "inline constexpr auto payload_manifest_hash = " + cpp_wide(package.digest(payload / "package-manifest.json")) + ";",
             "inline constexpr std::size_t longest_payload_path = " + str(max(len(name.encode("utf-16-le")) // 2 for name in files)) + ";",
             "inline constexpr PayloadEntry payload_entries[] = {"]
    for name, record in sorted(files.items()):
        package.safe_relative(name)
        lines.append("  {" + cpp_wide(name.replace("/", "\\")) + ', "' + record["sha256"] + '", ' + str(record["bytes"]) + "ull},")
    return "\n".join(lines + ["};", ""])


def generate_include(files):
    lines, previous = [], None
    for index, name in enumerate(sorted(files)):
        path = Path(name)
        directory = path.parent.as_posix()
        if directory != previous:
            suffix = "" if directory == "." else "\\" + nsis_string(directory.replace("/", "\\"))
            lines.append('SetOutPath "$PLUGINSDIR\\payload' + suffix + '"')
            previous = directory
        # Compile-time File paths do not use runtime dollar escaping. Numbered
        # inputs keep arbitrary host paths out of the NSIS preprocessor entirely.
        lines.append('File "/oname=' + nsis_string(path.name) + '" "staged-inputs/' + str(index) + '.bin"')
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default=os.environ.get("APO_INSTALLER_CXX", "x86_64-w64-mingw32-g++"))
    parser.add_argument("--makensis", default=os.environ.get("MAKENSIS", "makensis"))
    parser.add_argument("--objdump", default=os.environ.get("APO_OBJDUMP", "llvm-objdump"))
    parser.add_argument("--work", type=Path, help="New directory retained with generated inputs, helper and build record")
    args = parser.parse_args()
    payload, output = args.payload.absolute(), args.output.absolute()
    metadata, files = package.verify_payload(payload)
    package.require(output.suffix.lower() == ".exe" and not output.exists(), "Output must be a new .exe filename")
    package.ordinary_path(output)
    package.require(output.name.casefold() != "setup.exe", "Use a versioned installer filename instead of the Windows compatibility name setup.exe")
    tool_version = subprocess.check_output([args.makensis, "-VERSION"], text=True).strip()
    parsed = re.search(r"(\d+)\.(\d+)", tool_version)
    package.require(parsed and tuple(map(int, parsed.groups())) >= (3, 13), "NSIS 3.13 or newer is required")
    compiler_version = subprocess.check_output([args.compiler, "--version"], text=True).splitlines()[0]
    output.parent.mkdir(parents=True, exist_ok=True)
    if args.work:
        work = args.work.absolute()
        package.require(not work.exists(), "--work must name a new directory")
        package.ordinary_path(work)
        work.mkdir(parents=True)
    else:
        work = Path(tempfile.mkdtemp(prefix="apo-installer-", dir=output.parent))
    sources = ["packaging/apophysis-reborn.nsi", "packaging/installer_guard.cpp", "packaging/upgrade_receipt.hpp",
               "packaging/build_installer.py", "src/sha256.hpp"]
    source_hashes = {name: package.digest(ROOT / name) for name in sources}
    (work / "payload_manifest.hpp").write_text(generate_header(payload, metadata, files), encoding="utf-8")
    (work / "payload.nsh").write_text(generate_include(files), encoding="utf-8")
    staged = work / "staged-inputs"
    staged.mkdir()
    for index, name in enumerate(sorted(files)):
        destination = staged / (str(index) + ".bin")
        try:
            os.link(payload / name, destination)
        except OSError:
            shutil.copyfile(payload / name, destination)
        package.require(package.digest(destination) == files[name]["sha256"], "Staged installer input changed: " + name)
    shutil.copyfile(ROOT / "packaging/apophysis-reborn.nsi", work / "apophysis-reborn.nsi")
    helper = work / "installer-guard.exe"
    compile_command = [args.compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-static", "-municode",
                       "-D_WIN32_WINNT=0x0A00", "-DUNICODE", "-D_UNICODE", "-I" + str(ROOT / "src"), "-I" + str(work),
                       str(ROOT / "packaging/installer_guard.cpp"), "-o", str(helper),
                       "-lole32", "-lshell32", "-ladvapi32", "-luuid"]
    subprocess.run(compile_command, check=True)
    imports, _ = package.stage.inspect(args.objdump, helper)
    package.require(all(name in package.stage.WINDOWS_DLLS or name.startswith(("api-ms-win-", "ext-ms-win-"))
                        or name == "msvcrt.dll" for name in imports), "The private installer helper has a non-system DLL dependency")
    private_output = work / ("apophysis-reborn-" + metadata["version"] + "-windows-x64-setup.exe")
    driver = work / "installer.nsi"
    definitions = {"APO_VERSION": metadata["version"], "APO_OUTPUT": private_output.name,
                   "APO_GUARD": helper.name, "APO_PAYLOAD_INCLUDE": "payload.nsh"}
    driver.write_text("\n".join('!define ' + key + ' "' + nsis_string(value) + '"' for key, value in definitions.items()) +
                      '\n!include "apophysis-reborn.nsi"\n', encoding="utf-8")
    # Resolve the tool before changing cwd so caller-relative --makensis works.
    makensis = str(Path(shutil.which(args.makensis) or args.makensis).absolute())
    subprocess.run([makensis, "-NOCONFIG", "-WX", "-V3", driver.name], cwd=work, check=True)
    package.require(private_output.is_file() and private_output.stat().st_size > 65536, "Installer compiler did not produce a PE file")
    # A second complete read ensures the compiled files were not edited mid-build.
    package.verify_payload(payload)
    package.require(package.manifest_files(payload) == files, "Payload changed while compiling installer")
    package.require(package.digest(work / "apophysis-reborn.nsi") == package.digest(ROOT / "packaging/apophysis-reborn.nsi"),
                    "Installer template changed while compiling")
    package.require(all(package.digest(ROOT / name) == source_hashes[name] for name in sources),
                    "Installer sources changed while compiling")
    os.link(private_output, output)
    record = {"schema": "apophysis-reborn.installer-build", "schema_version": 1,
              "version": metadata["version"], "source_commit": metadata["source_commit"],
              "installer_sha256": package.digest(output), "installer_bytes": output.stat().st_size,
              "payload_manifest_sha256": package.digest(payload / "package-manifest.json"),
              "payload_files": len(files), "nsis": tool_version, "compiler": compiler_version,
              "helper_sha256": package.digest(helper), "helper_imports": sorted(imports),
              "source_sha256": source_hashes,
              "generated_input_sha256": {name: package.digest(work / name) for name in
                  ["payload_manifest.hpp", "payload.nsh", "installer.nsi"]},
              "windows_executed": False, "signed": False,
              "policy": "Per-user; remove the most recent previous version after successful installation; no file associations; empty destination; compiled inventory and hash-checked nonrecursive uninstall"}
    (work / "installer-build.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"installer": str(output), "work": str(work), **record}, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit("Installer build failed: " + str(error))
