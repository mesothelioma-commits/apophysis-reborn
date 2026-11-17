# Initial research notes (not a complete inventory)

- Apophysis 7X repository: https://github.com/xyrus02/apophysis-7x — Delphi code; archived July 2026 and points to Codeberg. Its GitHub repository lists GPL-3.0.
- FLAM3 repository: https://github.com/scottdraves/flam3 — GPL-3.0; README lists 98 indexed variations (0–97) and includes high-resolution rendering, animation and genome mutation utilities.
- Apophysis AV: https://sourceforge.net/projects/apophysis-av/ — GPLv3; advertised features include image-embedded parameters, enhanced scripting, mathematical expression interpretation, transform synchronization and frame-by-frame animation.

## Important outstanding work

Inspect original Apophysis 2.x source, obtain specific tagged snapshots of each variant, audit license files and third-party assets, inventory dialogs and parameters, and construct verified compatibility test fixtures. No historical compatibility claim is yet justified.

## Core Engine follow-up

The numerical baseline now pins FLAM3 commit `1cdfc77275821182ad3c057e629c75a240dd52d3`. The test suite compares against its C variation functions and a render independently produced by the complete upstream executable. [Reference checks, primary-source map and reproduction](REFERENCES.md)

A published Apophysis AV Phoenix flame exposed unsupported producer fields and a plugin requirement. One reduced copy was used only to verify a supported transform and the packed palette. [Exact reductions and outcome](COMPATIBILITY.md)
