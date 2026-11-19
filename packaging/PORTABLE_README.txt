Apophysis Reborn — Windows x64 runtime edition

Extract the whole folder before launching apophysis-reborn.exe. Keep its DLLs,
Qt plugins and qt.conf together. flame-cpu.exe is the command-line renderer.
apophysis-mcp.exe connects a local MCP client to a running editor. See
docs/LOCAL_AI.md for setup, permissions and the review workflow.
When replacing this folder, close the editor and stop the MCP client first.
The runtime works offline; no installer or administrator access is required.

Open docs/USER_MANUAL.html or use Help > User manual in the application.
Example flames and native animation projects are in examples/.
The render controls include editable PNG export and a final-resolution proof
for inspecting a region of a larger image. See docs/RELEASE_0.16.0.md for
the Windows walkthrough and docs/ROADMAP.md for upcoming builds.
Save your projects in a personal folder. Favorites are kept for the current
Windows user and can be opened after you replace or move the program folder.
Preferences, recovery data and queue state also use per-user storage.

The complete source/history/verification edition is a separate download. It
contains the native test programs, Git history and development build scripts.
This runtime edition includes the dependency notices and corresponding LGPL
source archives in licenses/ and third-party-sources/. The Vulkan loader and
graphics drivers are supplied by Windows; no GPU driver is bundled.

package-manifest.json records every payload file and its source checkpoint.
SHA256SUMS.txt can be checked from this folder in Git Bash:
  sha256sum -c SHA256SUMS.txt

Windows installation/execution and physical-GPU acceptance must be assessed on
their actual target systems; a cross-build or dependency audit does not establish
that acceptance. See docs/FEATURE_MATRIX.md and docs/TEST_PLAN.md for the current
release's scope and executed results.
