# Apophysis Reborn 0.3.0 — Legacy Compatibility

This Windows x64 development build opens legacy flame libraries, reports which entries the CPU engine can render, and saves them in their original XML encoding. Unedited saves preserve every byte. A change to a name, size, center or scale affects only the selected flame's opening tag. Unknown variations, plugin values, comments, internal history and other collection members remain in the saved document.

The package includes a [593 × 593 render](preview/fox-3d-hack.png) of Fox’s 2009 3D Hack flame and the original parameter listing. [Artwork attribution and reuse terms](tests/legacy/ATTRIBUTION.md)

## Work with a legacy file

Run these examples from Git Bash in the extracted project folder. All paths can contain spaces.

```bash
./bin/windows-x64/flame-cpu.exe inspect tests/legacy/fox-3d-hack.flame
./bin/windows-x64/flame-cpu.exe save tests/legacy/fox-3d-hack.flame --output my-copy.flame --name "My study"
./bin/windows-x64/flame-cpu.exe inspect my-copy.flame --json
```

| Command | Result |
| --- | --- |
| `inspect INPUT [--json]` | Lists entries, original names, declared producers, source lines, CPU renderability and the first render blocker for each entry |
| `save INPUT --output COPY [--index N]` | Writes the entire original document. Optional `--name TEXT`, `--size W H`, `--center X Y` and `--scale S` edit one entry. Indexing starts at 1 |
| `export INPUT --output SINGLE --index N` | Writes a canonical, standalone XML representation of one supported render model. This operation omits wrapper data, history, comments and editor-only metadata. Use `save` to retain the original document |
| `--flame INPUT --index N --output IMAGE.bmp` | Renders the selected entry. Unsupported features in other entries do not block it |

A `preserve-only` entry can still be renamed and saved. Make a copy before editing an original you wish to keep. Providing the input path as the save destination requests an in-place replacement. Failed validation leaves an existing destination intact. The CLI stages file writes next to the destination and replaces the output after a successful close.

## Run the Windows build

Extract the entire ZIP, open Git Bash in the extracted `apophysis-reborn` folder and run:

```bash
bash scripts/verify-windows.sh
```

This runs the reference, XML and rendering checks, renders Ember Spiral with one and four workers, compares the results, saves a timestamped text log under `logs/`, and opens Explorer with the BMP selected. It requires Git Bash and Windows x64. The prebuilt CLI does not require a development toolchain. Use `--no-open` to suppress Explorer.

Render a different file from the supported subset with:

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame --output ember.bmp
./bin/windows-x64/flame-cpu.exe --list-variations
```

The 21 studies in `examples/variation-studies.flame` cover every registered variation. Use `inspect` to list their names, then select a study by its one-based `--index`.

The CLI accepts `--index` (1-based), `--seed`, `--samples`, `--threads`, and `--memory-mib`. The flame provides the dimensions and camera. For the original foundation command syntax use `flame-cpu width height samples output.ppm`. Both BMP and PPM store RGB8. Use a fresh output name to keep both renders. A successful write replaces an existing output of the same name.

## Build from source

CMake 3.20+, a C++20 compiler with threading support, and Expat development files are required. On Windows, the supported distribution in this checkpoint is the cross-compiled executable. A native MSVC build can be attempted from Git Bash with the Visual Studio C++ workload and an Expat installation, for example the vcpkg `expat:x64-windows` port and `VCPKG_ROOT` set. Run:

```bash
bash scripts/build.sh
```

The script configures a release build, builds every executable and runs CTest. See [Windows build](docs/WINDOWS_BUILD.md) for the exact toolchain and static Expat configuration used to produce the shipped binaries.

## Included records

| File | What it covers |
| --- | --- |
| [Engine](docs/RENDERING_ENGINE.md) | How inputs become pixels, accepted numeric settings, rendering differences |
| [Architecture](docs/ARCHITECTURE.md) | Model, parser, sampling, image output and extension points |
| [Compatibility](docs/COMPATIBILITY.md) | Exact XML subset, omitted-field behavior, serializer limits, historical files |
| [Historical corpus](docs/LEGACY_CORPUS.md) | Original producer files, exact round trips and reference-image evidence |
| [Producer pixel tests](docs/PRODUCER_ORACLE.md) | Direct comparisons with historical Pascal image reconstruction |
| [Reference tests](docs/REFERENCES.md) | Upstream C function comparisons, independently rendered image, regeneration |
| [Test report and plan](docs/TEST_PLAN.md) | Executed checks, local timings and pending platform acceptance |
| [Feature matrix](docs/FEATURE_MATRIX.md) | Current milestone and outstanding work |
| [Development log](docs/DEVELOPMENT_LOG.md) | Changes since the foundation and next milestone handoff |
| [Third-party notices](docs/LICENSES.md) | Dependency and reference provenance |

The ZIP also includes `apophysis-reborn.bundle` and `BUILD.txt`. For a checkout with the complete checkpoint history, run `git clone apophysis-reborn.bundle ../apophysis-reborn-git` from the extracted folder. The checkout holds source, example flames, tests and documentation. The precompiled Windows executables are distribution outputs. No remote repository is required.

Milestone 4 is the primary editor: Qt main window, flame library, live CPU preview and transform editor. Windows 11 runtime verification is deferred. Keep the original 0.2.0 ZIP if you want to compare the earlier engine checkpoint.
