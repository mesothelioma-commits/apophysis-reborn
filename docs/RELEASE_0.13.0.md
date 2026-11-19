# Apophysis Reborn 0.13.0

This release brings an eight-flame starter library to the desktop editor. Apophysis Reborn is an independent clone of the original Apophysis fractal flame editor by Mark Townsend and contributors. AI generated the modern Reborn implementation and its documentation. The repository owner directed the project and publishes its builds. They did not personally write the application code.

## Run this release

Download the Windows x64 installer or the portable ZIP. Setup installs for the current Windows user and is unsigned. For portable use, extract the complete ZIP and run `apophysis-reborn.exe` in the extracted application folder. Both editions include the CLI, libraries, Qt plugins, examples, offline documentation, and dependency notices and sources. The illustrated PDF walks through your first render. The Windows test kit produces a local results ZIP for test feedback. [Release downloads](https://github.com/mesothelioma-commits/apophysis-reborn/releases/tag/v0.13.0) · [First-session guide](https://github.com/mesothelioma-commits/apophysis-reborn/blob/main/docs/Apophysis-Reborn-0.13.0-User-Guide.pdf) · [Fork with an AI assistant](https://github.com/mesothelioma-commits/apophysis-reborn/blob/main/docs/AI_FORK_GUIDE.md)

## Starter collection

Launch without a filename to open Ember spiral, Glass fern, Ruby rosette, Prism lace, Tidal filaments, Copper dragon, Violet nebula, and Golden nautilus. Click a thumbnail or search by name. A narrow library dock wraps the names beneath the thumbnails. **File → Open starter library** reloads the embedded collection. **File → Save as…** saves an editable copy. The same collection ships as `examples/starter-library.flame`.

The Windows binaries were built from commit `0e71c19aeaf59babb7a8c3fafce8bd5b6f92e61f`, tagged `v0.13.0-starter-library-recovery`. Ember spiral and Tidal filaments use the existing project definitions. The six other starter designs were recreated for this recovered release.

## Verification and limits

All 49 automated suites passed on the tagged Linux build. The eight starter flames rendered on the CPU and on a software Vulkan device. Static verification compared 249 payload files in the Windows installer and portable ZIP. Keep these results separate from physical GPU performance or Windows installer lifecycle testing. Historical legacy-file compatibility remains partial, particularly for 3D and direct-color files. [Compatibility report](https://github.com/mesothelioma-commits/apophysis-reborn/blob/main/docs/RELEASE_COMPATIBILITY.md)

If you report a problem, give the exact application version, operating system, renderer shown by the application, steps to reproduce, and expected versus actual behavior. **Help → System information…** can save a local diagnostic report. The test kit can also exercise seed/save/reopen, closing Mutate, and resizing Live Preview. Include only the examples and diagnostics you intend to share.
