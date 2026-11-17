# Dependency and reference notices

The Windows console binaries link Expat 2.10.0 and the LLVM-MinGW C++/threading runtime statically. The distribution includes the upstream notices under `licenses/windows/`, including Expat, LLVM, compiler-rt, libc++, libc++abi, libunwind, MinGW-w64, and winpthreads. Native source builds use the Expat installation selected by CMake. See WINDOWS_BUILD.md for exact build inputs and hashes.

FLAM3 by Scott Draves and contributors is the independent numerical and image oracle. The packaged CPU renderer builds from the C++ files in src. The optional numeric-vector regeneration script reads a separately obtained FLAM3 source tree into a temporary test build. The selected spherical coefficients in the two test fixtures originate in FLAM3's test.flam3. The reference image was rendered by the upstream executable from the adapted fixture, whose camera and embedded palette are recorded in REFERENCES.md. Upstream licensing text and attribution accompany the fixtures in `tests/reference-FLAM3-LICENSE.txt` and `docs/REFERENCES.md`. [Upstream FLAM3](https://github.com/scottdraves/flam3)

The Apophysis AV corpus spot check uses a separately published flame, identified in LEGACY_CORPUS.md. Neither the original artwork nor the reduced private test copy is bundled. The project-wide audit for legacy plugin, palette, UI and other asset reuse remains open.


The 0.3.0 historical corpus records asset-specific provenance in tests/legacy/ATTRIBUTION.md and docs/LEGACY_CORPUS.md. The Fox parameter listing and the new rendered preview carry CC BY-SA 4.0 attribution in tests/legacy/ATTRIBUTION.md. External artist images and the original application release archives remain available from their publishers. The optional producer-oracle regeneration harness reads a separately obtained, hash-checked Apophysis Pascal source tree. Its controlled output files, source references and test scope are described in docs/PRODUCER_ORACLE.md.
