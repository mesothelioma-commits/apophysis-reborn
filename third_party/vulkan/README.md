# Vulkan C headers

This directory contains unmodified C interface headers from the Khronos Group's
[Vulkan-Headers v1.4.309](https://github.com/KhronosGroup/Vulkan-Headers/tree/v1.4.309).
The subset includes `vulkan.h`, `vulkan_core.h`, `vk_platform.h`, and the video
codec headers referenced by `vulkan_core.h`. It omits the C++ bindings and
platform-specific surface headers; the renderer uses headless compute.

The application loads the operating system's Vulkan loader dynamically. These
headers do not bundle a loader, GPU driver, software Vulkan driver, shader
compiler or validation layer. A source build therefore does not need a Vulkan
SDK merely to compile the existing compute backend and embedded shader.

## Provenance and license

- Upstream tag: `v1.4.309`.
- Source archive: https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/v1.4.309
- Archive SHA-256: `437925ada160d86ed763d29dcb9318c1bb0d024d7deaf77bc7c170b8eb6b6f10`.
- The header files are copied verbatim. `SHA256SUMS.txt` records the included
  bytes. The upstream `LICENSE.md` is retained as `LICENSE`, and both complete
  license texts are retained under `LICENSES`.
- The included C headers carry Apache-2.0 SPDX identifiers. Both upstream
  license texts are retained, and copyright notices remain in each source file.

The runtime uses Vulkan 1.0 compute features; the header version is not a
minimum GPU API version. Optional capabilities must still be queried on each
reported device. Third-party shader compilers and test drivers used during
verification are development tools and are documented separately.
