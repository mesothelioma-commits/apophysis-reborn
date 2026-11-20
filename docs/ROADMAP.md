# Apophysis Reborn roadmap

| Build or work area | Status | Artist workflow |
| --- | --- | --- |
| 0.14.0 | Released | Connect a local MCP client, inspect the selected flame, and review a proposed Lua edit in the desktop editor. |
| 0.15.0 | Released | Save editable favorites with tags across projects. Browse built-in variations and compare previews of one weight or parameter at a fixed seed. |
| 0.16.0 | Testing build | Export an optional editable flame in a PNG. Inspect a final-quality crop at the intended output size with a transparency checkerboard. Reuse samples for compatible tone adjustments. |
| 0.17.0 | Source integrated; Windows packaging pending | Edit individual animation properties, render classic 2D flames, compose flame layers, and export animation to MP4. |

## 0.17.0 verification and packaging

Windows x64 and Linux native CI passed on the implementation commit. Remaining release work includes the Windows installer and portable ZIP, an interactive Windows upgrade test and physical GPU verification. The CPU and software Vulkan images for the historical Splits Elliptic flame differ visibly at the tested sample budget. [Build notes and measured comparison](RELEASE_0.17.0.md)

Build deliveries include the download, changes, test results, outstanding platform checks, and the current roadmap. Update this table when a milestone ships or priorities change.
