# Fork and build Apophysis Reborn with an AI assistant

Use a GitHub fork when you want an AI coding assistant to modify your own copy. The Windows installer and portable ZIP are for running the released application. A source checkout gives the assistant the code, fixtures, build scripts, and edit history it needs to make a change you can review.

## Create your own copy

1. Sign in to GitHub, open the repository, and choose **Fork**. Select your account as the owner and create the fork. [Apophysis Reborn on GitHub](https://github.com/mesothelioma-commits/apophysis-reborn)
2. From **Code** in **your fork**, copy the HTTPS clone URL. Open Git Bash on Windows, or Bash on Linux, in the folder where you keep projects. Run the following and paste your fork’s URL at the prompt.

   ```bash
   read -r -p "Paste your fork's HTTPS clone URL: " fork_url
   git clone "$fork_url" apophysis-reborn
   cd apophysis-reborn
   git remote add upstream https://github.com/mesothelioma-commits/apophysis-reborn.git
   git fetch upstream --tags
   git switch -c work/first-change v0.14.0
   git remote -v
   ```

   `origin` should point to your fork and `upstream` to the published project. The new branch starts at the exact source used for the 0.14.0 Windows release. Run `git show --no-patch v0.14.0` to see the release commit. To start from later upstream work, create your branch from `upstream/main`.

3. Open the cloned folder in your AI coding tool. With a hosted tool, select your fork or connect it and grant access to that repository. Select your work branch, or have the assistant make a new branch in your fork. Give the tool a specific change, a supported platform, and a way you can tell the feature works.

## Build and test the baseline

For the full editor, install Git, Bash, CMake 3.20 or newer, a C++20 compiler, Qt 6.5 or newer with Widgets, Network and Test, Expat and zlib development packages, and Python 3.10 or newer for the test suites. Use libraries built for the same architecture and compiler as the application. The Windows package was built with Qt 6.8.2. The normal source build includes Vulkan headers and a compiled shader. [Desktop dependencies and toolchain details](QT_BUILD.md)

On **Windows**, open Git Bash in an environment with the compiler and libraries for your Qt kit available. If CMake cannot discover Qt, set the prefix to the actual kit folder containing `bin` and `lib/cmake/Qt6`. The following prompt handles a Windows or Git Bash path. It also puts the kit’s runtime DLLs on the path for this session.

```bash
read -r -p "Path to your installed desktop Qt kit: " qt_kit
export APO_QT_PREFIX="$(cygpath -am "$qt_kit")"
export PATH="$(cygpath -au "$qt_kit")/bin:$PATH"
```

On **Linux**, install your distribution’s Qt, Expat, and zlib development packages. If Qt is installed outside the usual search paths, set `APO_QT_PREFIX` to the desktop kit prefix. Run these commands from the checkout on either platform.

```bash
bash scripts/check-deps.sh --no-open
bash scripts/build-editor.sh --build-dir build-release --jobs 2 --no-open
bash scripts/run-editor.sh --build-dir build-release --no-open
```

The first command configures a temporary build to check dependencies. The second configures and compiles the editor, renderer, and tests, then runs CTest. The third opens the editor. The wrappers save text logs in `logs/`. On a headless Linux build host, use Qt’s offscreen platform for the tests and leave the visual inspection for a desktop session.

```bash
QT_QPA_PLATFORM=offscreen bash scripts/build-editor.sh --build-dir build-headless --jobs 2 --no-open
```

A renderer-only project can use `bash scripts/build.sh --build-dir build-core --jobs 2 --no-open -DAPO_BUILD_EDITOR=OFF`. For Windows ABI choices, packaging inputs, or staging the runtime DLLs and plugins, use the release handoff. Its cross-build commands give the exact toolchain approach used to produce the Windows packages. [Windows build notes](WINDOWS_BUILD.md) · [Release handoff](RELEASE_HANDOFF.md)

## Give the assistant one task

Replace the example request with your own. This prompt works with an assistant that can edit a local Git checkout or a connected repository.

```text
Work in my fork of Apophysis Reborn on a feature branch. My change is:
[Describe one feature, bug, or visible behavior and how I will verify it.]

Read README.md, docs/ARCHITECTURE.md, docs/RELEASE_COMPATIBILITY.md, and
the user manual section for that feature. Report the branch, baseline
revision, and test environment. Build and test the baseline, implement the
change in the relevant source, and run the affected tests plus the normal
suite. Preserve existing .flame and .apoproject behavior unless my request
explicitly changes it. Add a focused regression test if existing tests
would miss the bug. Update the user-facing instructions if a control changes.

Give me the diff or a reviewable pull request against my fork. Summarize
what you actually ran, what failed or was skipped, and what I should test
on Windows or a physical GPU. Keep the original Apophysis and FLAM3
credits, AI authorship disclosure, and all dependency and asset notices.
```

For example, ask for a keyboard shortcut that focuses the existing Flame library search box. State that typing after the shortcut should filter the list and that the shortcut must leave the selected flame unchanged. To investigate a rendering bug, supply a small shareable flame, the seed, dimensions, sample budget, expected output, and actual CPU or Vulkan backend.

## Review and distribute a changed version

Review `git diff`, open your test project, save it under a new name, reopen it, and export a PNG. A Linux or software-Vulkan pass gives scoped evidence. To test a Windows package on a real device, run the Windows test kit on an eligible release build, or use the source verification suites and package scripts for your fork’s changed version. The supplied Windows test kit checks the 0.13.0 build identity.

When your commit is ready, run `git push -u origin HEAD`. On GitHub, choose **your fork’s main branch** as the pull request base if you want the change only in your own project.

For a distributable build, set the intended application version, commit and tag the tested source, rebuild from that commit, and produce new installer and portable assets from the same staged runtime. Include checksums, the exact revision, actual test outcomes, platform limits, and corresponding sources. Keep the original Apophysis credits, Reborn’s AI disclosure, and all relevant license notices. The 0.14.0 tree has component-specific license records and no project-wide license for Reborn’s own implementation. Review the applicable terms before distributing your version. [Packaging](../packaging/README.md) · [Notices and provenance](LICENSES.md)
