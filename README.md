# Apophysis Reborn

**Apophysis Reborn is an unofficial, AI-generated clone of the original Apophysis fractal flame editor.** The original Apophysis was created by Mark Townsend and contributors. Reborn recreates its editing and rendering workflow in C++20 and Qt 6 for modern CPUs and Vulkan-capable GPUs. AI generated the Reborn application code and documentation. The repository owner directed and published the project. They did not personally write the application code. The original creators and third-party contributors retain credit for their own work.

![Eight starter flames in Apophysis Reborn 0.13.0](preview/starter-library-0.13.0.png)

## Download for Windows 11 x64

| Download | How to use it |
| --- | --- |
| [Installer](https://github.com/mesothelioma-commits/apophysis-reborn/releases/download/v0.13.0/apophysis-reborn-0.13.0-windows-x64-setup.exe) | Close the editor and run Setup. It installs for the current Windows user. The installer is unsigned. |
| [Portable ZIP](https://github.com/mesothelioma-commits/apophysis-reborn/releases/download/v0.13.0/apophysis-reborn-0.13.0-windows-x64-portable.zip) | Choose **Extract All** in File Explorer. Inside the extracted application folder, open **apophysis-reborn.exe**. Keep it with its DLLs, plugins, and examples. |
| [Source used for the Windows binaries](https://github.com/mesothelioma-commits/apophysis-reborn/releases/download/v0.13.0/apophysis-reborn-0.13.0-git-source.zip) | Includes the matching source tree and a restorable Git history. Clone the repository’s main branch for the latest public documentation. |
| [Illustrated user guide (PDF)](docs/Apophysis-Reborn-0.13.0-User-Guide.pdf) | Pages 1–4 lead from installation to a finished PNG. Page 8 has a fillable tester sheet. |
| [Windows test kit](https://github.com/mesothelioma-commits/apophysis-reborn/releases/download/v0.13.0/Apophysis-Reborn-0.13.0-Windows-Test-Kit.zip) | With Git for Windows installed, extract this separate kit and open **Start-Tests.cmd**. It checks the installed or portable build and saves a local results archive. [Test scripts](tools/windows-test-kit/) |

Both Windows editions run offline. CPU rendering works without a Vulkan driver. For GPU sampling, use a current driver that exposes Vulkan on your GPU. Setup installs the new payload before attempting to retire the most recent older registered version. Save artwork in a folder of your own. [Release assets and checksums](https://github.com/mesothelioma-commits/apophysis-reborn/releases/tag/v0.13.0)

## Make a first image

1. Launch the application and select **Ember spiral** in the Flame library. The center pane shows its live preview.
2. Choose **File → Save as…** and save a working copy as `First flame.apoproject`.
3. Open **Gradient**, select **Glacier**, click **Use preset**, then **Apply gradient**. Press **Ctrl+S** to keep the edit.
4. Open **File → Render image…**. For this starter, try **768 × 576** and **PNG · 8 bit**. Browse for an output filename and click **Add and start queue**.
5. When the queue shows **Complete**, open the PNG in File Explorer. Reopen the project when you want to adjust the drawing rules or colors.

Press **F1** in the editor for offline documentation. [User manual](docs/USER_MANUAL.md)

## What works in 0.13.0

- Edit eight starter flames with affine transforms, palettes, tone curves, variations, mutation, and a seeded random generator. The renderer implements 43 built-in variations.
- Use multithreaded CPU rendering or Vulkan sampling. Render in strips, queue images, and resume compatible checkpoints. The image size limit is 16,384 pixels per side.
- Save editable native projects, preserve supported parts of legacy flame libraries, and export PNG, TIFF, PPM, or BMP images. Use the timeline for numbered frame sequences or the command-line renderer for batch work.

Legacy compatibility varies by producer and feature. Some documents can be preserved on save while an unsupported operation prevents rendering. Historical 3D and direct-color behavior, some filters, and parts of the original scripting and plugin workflows remain incomplete. See the compatibility report before depending on a specific older flame. [Compatibility and outstanding work](docs/RELEASE_COMPATIBILITY.md)

The recovered 0.13.0 application commit is `0e71c19aeaf59babb7a8c3fafce8bd5b6f92e61f`. All 49 automated test suites passed on Linux for this build. The installer and portable payloads were compared file by file. Native Windows upgrade behavior and physical GPU coverage still need target-machine testing. [Release notes](docs/RELEASE_0.13.0.md)

## Build a fork with an AI assistant

Start with the fork guide for a copy-and-paste prompt, Git commands, development dependencies, a baseline build, tests, and instructions for packaging a changed version. [Fork and build guide](docs/AI_FORK_GUIDE.md)

| I want to… | Start here |
| --- | --- |
| Understand the editor and renderer | [Architecture](docs/ARCHITECTURE.md) · [Rendering engine](docs/RENDERING_ENGINE.md) · [Hardware acceleration](docs/GPU_RENDERING.md) |
| Work with legacy files or variations | [File compatibility](docs/COMPATIBILITY.md) · [Variation reference](docs/VARIATIONS.md) |
| Build, test, or package for Windows | [Qt desktop build](docs/QT_BUILD.md) · [Release handoff](docs/RELEASE_HANDOFF.md) · [Packaging](packaging/README.md) |
| Explore previous milestones and engineering notes | [Technical notes](TECHNICAL-NOTES.md) · [Development history](docs/DEVELOPMENT_LOG.md) |

## Credits and licenses

The historical references include the original Apophysis editor, Apophysis 7X, Apophysis AV, and FLAM3 by Scott Draves and contributors. FLAM3 supplies published reference formulas and test material used for numerical and image comparisons. Reborn runs its own C++ renderer. [Original Apophysis](https://sourceforge.net/projects/apophysis/) · [Apophysis 7X](https://github.com/xyrus02/apophysis-7x) · [Apophysis AV](https://sourceforge.net/projects/apophysis-av/) · [FLAM3](https://github.com/scottdraves/flam3)

The repository records licenses and attribution separately for dependencies, historical fixtures, and credited artwork. This release has no project-wide license for the Reborn implementation. Check the applicable terms before redistributing a modified build, and keep the accompanying notices and corresponding dependency sources with packaged releases. [Dependency and reference notices](docs/LICENSES.md) · [Historical asset attribution](tests/legacy/ATTRIBUTION.md) · [License texts](licenses/)
