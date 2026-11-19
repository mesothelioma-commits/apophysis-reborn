# Windows runtime and installer packaging

Apophysis Reborn 0.14.0 ships a local AI companion alongside the Windows editor and command-line renderer. [Local AI setup](../docs/LOCAL_AI.md) covers client configuration and editor permissions.

## Outputs and requirements

The Windows runtime contains `apophysis-reborn.exe`, `flame-cpu.exe` and
`apophysis-mcp.exe`, their DLL dependencies (including Qt Network), required Qt
plugins, the Reborn Wave SDK sample, examples,
offline documentation, notices and bundled corresponding-source archives.
Application executables are at the runtime folder root. Test executables,
test-only DLLs and historical large rendered images belong to the separate full
source/SDK release.

| Output | Purpose |
| --- | --- |
| `apophysis-reborn-0.14.0-windows-x64/` | Exact, audited payload shared by the ZIP and installer |
| `apophysis-reborn-0.14.0-windows-x64-portable.zip` | Relocatable application distribution |
| `apophysis-reborn-0.14.0-windows-x64-setup.exe` | Per-user Windows installer |
| `package-manifest.json` and `SHA256SUMS.txt` | Runtime identity, inventory, lengths and hashes |
| Installer work directory `installer-build.json` | Tool versions, helper imports, source/input/output hashes and execution limits |

Build prerequisites are Python 3.10+, NSIS 3.13+, a MinGW-w64 C++20 compiler with
Windows SDK headers/libraries, and PE-capable `llvm-objdump` or GNU `objdump`.
The recorded compiler used for this checkpoint is LLVM-MinGW Clang 23.1.5.
The NSIS compiler runs on Windows or Linux. The helper is a statically linked
Windows x64 executable with only Windows system DLL imports.

## Create a payload and portable ZIP

Freeze the release source first. Supply the full source commit used to build the
staged applications; do not substitute a later documentation or packaging commit.
The full release auditor separately verifies the compiled application identity.

```bash
python3 /path/to/apophysis-reborn/scripts/package-runtime.py \
  --runtime /path/to/windows-staging \
  --output /path/to/new-release \
  --lgpl-sources /path/to/corresponding-sources \
  --objdump /path/to/llvm-objdump \
  --source-commit FULL_40_CHARACTER_GIT_COMMIT \
  --source-date-epoch COMMIT_UNIX_TIMESTAMP
```

The output directory may exist, but the new payload folder and ZIP must not.
The corresponding-source directory must contain its `SHA256SUMS.txt`, listed
source archives including Qt Base, and optionally `README.txt`. Unlisted or
modified source archives are refused. Included notices and sources are recorded
as package inventory; this is not a legal compliance certification.

The packager inspects imported DLL names and verifies non-system imported symbols
against actual exported symbols. It never substitutes test DLLs for application
dependencies or distributes the system Vulkan loader. Paths are checked for
Windows device names, traversal, case/file-directory collisions and excessive
length. Symbolic links and Windows reparse paths are refused before traversal.

```bash
python3 /path/to/apophysis-reborn/scripts/package-runtime.py \
  --verify /path/to/new-release/apophysis-reborn-0.14.0-windows-x64
```

Verification checks every file, the exact inventory, sizes and both checksum
layers. ZIP entries are streamed with stable order, timestamps and modes, then
read back and hashed. Equal payload bytes and source-date epoch produce equal
ZIP bytes.

## Build an installer

```bash
bash /path/to/apophysis-reborn/scripts/build-installer.sh \
  --payload /path/to/new-release/apophysis-reborn-0.14.0-windows-x64 \
  --output /path/to/new-release/apophysis-reborn-0.14.0-windows-x64-setup.exe \
  --compiler /path/to/x86_64-w64-mingw32-clang++ \
  --makensis /path/to/makensis \
  --objdump /path/to/llvm-objdump \
  --work /path/to/new-installer-work \
  --no-open
```

The wrapper works from any current directory. Relative argument paths remain
relative to that directory. It writes a timestamped `.txt` log under project
`logs/`, or `APO_LOG_DIR` when set. On Windows it selects the finished installer
in Explorer unless `--no-open` is supplied. A missing `--work` creates a retained
private work directory beside the output. Output files and explicit work
directories must be new. If using unpacked Linux NSIS packages, set `NSISDIR` to
their `usr/share/nsis` directory.

NSIS warnings fail the build. The compiler runs with `-NOCONFIG` and receives only
numbered, relative private input filenames, so host path text cannot become NSIS
source. Runtime payload names use separate preprocessor and runtime escaping,
including literal dollar signs, `${...}` and `$%...%`. Source and payload hashes
are checked again after compilation. This does not promise byte-reproducible
installer EXEs; PE/NSIS compiler timestamps are recorded through artifact hashes.

## Installation and removal policy

The default destination is
`%LOCALAPPDATA%\Programs\Apophysis Reborn\0.14.0`. Setup requests user privilege,
writes only a current-user uninstall entry and uses versioned folders and
shortcuts. It supports Windows x64, Windows 10 or newer; Windows 11 is the primary
acceptance target. It creates no file associations. Start Menu shortcuts are
selected by default; the desktop shortcut is an explicit choice. Silent setup
supports `/NOSTARTMENU` and `/DESKTOP`. NSIS `/D=` must be the final argument,
without surrounding quotes even when its value contains spaces.

The target must be new or empty, on a local drive, without reparse ancestors.
The 0.12.0 installer compares registered release numbers numerically. Installers published through 0.11.0 keep versions side by side. When the
incoming version is newer than the highest registered Apophysis Reborn version
for the current user, Setup installs and registers the new version before
removing that single previous installation. Its custom installation folder is
supported. Additional historical releases, portable copies and other Windows
accounts keep their installations. Installing an older version preserves the
newer version. The existing same-version registration check still applies.

Before upgrading, exit the editor and stop the MCP server in your AI client to release the Windows executable handles. If the client configuration uses an absolute versioned path, point it at the new copy of `apophysis-mcp.exe` after the upgrade.

If the new installation fails or the user cancels before it completes, the
previous installation stays in place. If a running program locks the previous
installation, close the program and use **Settings → Apps → Installed apps** to
uninstall the older version. The new release remains installed. Silent Setup
returns exit code `20` when the new installation succeeded and removal of the
previous release needs attention.

A forced termination can leave a partial new folder. Inspect it or choose another
empty destination. Detected install failures roll back only files that this attempt created.

`installer_guard.cpp` compiles the payload inventory and hashes into the helper.
During an upgrade, the helper verifies the previous release’s manifest against
the SHA-256 digest saved when that release was installed. It uses that verified
inventory to identify the previous release’s files. The guard holds
directory handles that deny deletion/replacement, creates files with `CREATE_NEW`,
and keeps verified file handles through the operation. Before uninstall deletes
the first file, it checks every remaining candidate. Locked ordinary files stop
the operation. Modified, read-only, hard-linked or replaced entries are retained;
unmodified owned files are removed through their verified handles. Directories
are removed only when empty, without recursive deletion. The helper never opens
the application's preference/recovery/queue stores for writing.

User projects and settings outside the program folder are retained. Modified or
additional files inside it are retained too. Retained files can prevent removal
of the version folder; the completed uninstall reports that fact. Installation
and uninstall logs are ordinary `.txt` files in the invoking account's temporary
directory. The wizard displays the log path; silent runs return an exit status
without error message boxes.

## Verification and acceptance boundary

Run host tests with `python3 tests/packaging_tests.py`. These cover dependency and
export failures, exact inventories, archive reproducibility, source checksums,
path conflicts, symlink/reparse admission and generated-input escaping. A real
compile/extract probe additionally verifies literal dollar/macro/environment and
Unicode payload names. It uses synthetic bytes, so it is not a release runtime.

Use a disposable Windows x64 account with no registered Apophysis Reborn
installations. Close any running Apophysis processes. The evidence directory
must be new and its absolute path shorter than 90 characters. The verifier
checks for existing registrations and conflicting shortcuts before it begins.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts/verify-installer-windows.ps1 `
  -Installer C:\release\apophysis-reborn-0.14.0-windows-x64-setup.exe `
  -Payload C:\release\apophysis-reborn-0.14.0-windows-x64 `
  -OutDirectory C:\temp\apo-installer-check
```

The equivalent Git Bash entry point resolves its own location and converts
argument paths explicitly before calling Windows PowerShell:

```bash
bash /path/to/apophysis-reborn/scripts/verify-installer-windows.sh \
  --installer /c/release/apophysis-reborn-0.14.0-windows-x64-setup.exe \
  --payload /c/release/apophysis-reborn-0.14.0-windows-x64 \
  --out /c/temp/apo-installer-check \
  --no-open
```

To test an upgrade, pass `--previous-installer /path/to/older-setup.exe`
or use `-PreviousInstaller` in PowerShell. The verifier installs the older
release in a custom folder, checks a successful upgrade and exercises the
recovery path when an older executable is locked.

The verifier retains `installer-lifecycle.json`, a `.txt` transcript and setup
logs. It exercises conflicting/nonempty destinations, junction ancestors, shortcut
conflicts and rollback, clean Unicode/space-path installation, byte-for-byte
payload verification, duplicate-version refusal, locked-file preflight, inner
directory-junction refusal, modified/extra/read-only/hard-linked preservation,
manifest edits, clean reinstall, optional shortcuts and clean uninstall. It
hashes watched current-user settings/association/other-version registry keys
before and after. Its intentionally retained files are available for inspection.
`-NoOpen` suppresses Explorer selection of the JSON report.

Actual Windows lifecycle execution, interactive wizard/desktop behavior,
reboot/power-loss scenarios, genuine previous-release upgrade testing, endpoint
security behavior, signing and platform acceptance remain separate release
gates. A Linux PowerShell syntax parse, PE import audit, NSIS compile or failed
Wine startup does not satisfy them.

## Upstream references and notices

- [NSIS scripting reference](https://nsis.sourceforge.io/Docs/Chapter4.html)
- [NSIS compile-time commands and Unicode predefines](https://nsis.sourceforge.io/Docs/Chapter5.html)
- [NSIS command-line usage](https://nsis.sourceforge.io/Docs/Chapter3.html)
- [Microsoft CreateFileW](https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-createfilew)
- [Microsoft reparse-point operations](https://learn.microsoft.com/windows/win32/fileio/reparse-point-operations)
- [PowerShell Start-Process and descendant waiting](https://learn.microsoft.com/powershell/module/microsoft.powershell.management/start-process?view=powershell-5.1)

NSIS notices are in [`notices/NSIS-LICENSE.txt`](notices/NSIS-LICENSE.txt).
The downloaded compiler/source provenance is in
[`notices/nsis-provenance.json`](notices/nsis-provenance.json). Third-party tools
stay outside the repository; their compiler runtime notices already reside in
`licenses/windows/`.
