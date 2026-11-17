# Historical flame corpus

Milestone 3 exercises 19 entries in five historical documents. Three are original downloadable files (17 entries). The other two are complete XML listings published by their creators. The verifier checks the producer embedded in each entry, preserves the source bytes on an ordinary save, changes a single selected name without altering surrounding bytes, and reopens both outputs. Renderability is reported separately for each entry.

| Source document | Embedded producer | Provenance and coverage | Rendered entries (one-based) |
| --- | --- | --- | --- |
| `Apophysis7X.temp` | Apophysis 2.08 beta 2 | Five base forms distributed in the Apophysis 7X 15D ZIP. The release filename identifies the distributor. The embedded XML identifies the 2.x writer. The original uses a capitalized `<Flames>` root and CRLF. [Publisher release](https://github.com/xyrus02/apophysis-7x/releases/tag/releases/7x15d) | 1–5 |
| `ApophysisAV.temp` | Apophysis AV Phoenix Edition | Eleven base forms shipped with Alice Vital’s Apophysis AV 1.0.2. Preserved UTF-8 BOM and CRLF. [Publisher release](https://sourceforge.net/projects/apophysis-av/files/Apophysis%20AV_v_1.0.2.zip/) | 1, 2, 3, 6, 10, 11 |
| `starfield.flame` | Apophysis 7x | Cedar Sanderson links the original seven-transform, 2750 × 1975 parameter file at the start of her April 2022 tutorial. Preserved UTF-8 BOM and CRLF. [Creator’s tutorial and download](https://madgeniusclub.com/2022/04/02/creator-mode-art-tutorial/) | 1 |
| `samj-demo.flame` | Apophysis AV Phoenix Edition | samj Créations publishes the XML and full-size result together. The verifier removes HTML layout whitespace and converts nonbreaking spaces to spaces. The hash identifies that repeatable extraction. [Creator’s parameters and result](https://samjcreations.blogspot.com/2022/08/apophysis-av.html?m=0) | 1 |
| `fox-3d-hack.flame` | Apophysis 2.08 3D hack | Fox published the XML on Wikimedia Commons in 2009. Its final ordinary transform combines JuliaN with linear3D. The source and attribution ship with the tests. [Permanent revision](https://commons.wikimedia.org/w/index.php?title=User:Fox/Fractal&oldid=246900092) | 1 |

A successful render check creates a nonuniform RGB PPM at the dimensions declared in the source. A preserve-only entry must have a diagnostic and the CLI must refuse a render without creating the requested output. All 19 entries passed the copy, rename and reopen checks. Fourteen render. The five preserve-only AV entries report non-default color curves (4 and 5), `splits` (7), `elliptic` (8) or `gaussian_blur` (9) as their first unsupported feature. Opening a saved copy in a historical Windows executable remains unverified.

## Publisher image comparisons

**Apophysis AV.** The Demo article pairs its XML with an 800 × 800 RGB PNG labeled “Résultat”. Reborn renders the source at that resolution with 8,000,000 samples, four workers and seed 123456. Against all 640,000 corresponding pixels, the mean absolute RGB channel error was **1.214890 levels out of 255**. Pearson correlation of each pixel’s mean RGB intensity was **0.988834**. Both original rasters are compared at their encoded coordinates and brightness. The author’s seed, sample budget and processing history are unspecified. The check allows an error of at most 3 levels and a correlation of at least 0.97 for this image. [Author’s article](https://samjcreations.blogspot.com/2022/08/apophysis-av.html?m=0)

**Apophysis 7X.** Sanderson’s first screenshot shows the starting flame in the editor before the tutorial edits. Its 954 × 857 JPEG has a 764 × 549 preview, bounded by the application’s gray surround. Coordinates in Pillow’s half-open convention are `(184, 267, 948, 816)`. The verifier renders all 2750 × 1975 source pixels at 40,000,000 samples, four workers and seed 123456, then box-downsamples the result to the preview dimensions. It measures the original viewport and also reports a 96 × 69 comparison to characterize broad structure. The crop removes application chrome and there is no color adjustment or additional image registration. At the native viewport size, mean absolute RGB error was **12.181279/255** and intensity correlation was **0.689425**. At 96 × 69 the values were **8.492905/255** and **0.983465**. The broad structure aligns while individual pixel intensity and tone differ. The comparison gate allows up to 17.5 levels of viewport error and requires correlations of at least 0.60 at viewport size and 0.95 at 96 × 69. The artist’s preview seed, exact sampling budget and JPEG settings are unknown. The later composited artwork in the tutorial underwent further edits. [Starting editor screenshot](https://madgeniusclub.com/wp-content/uploads/2022/04/starfield-tut-1.jpg) · [Creator’s tutorial](https://madgeniusclub.com/2022/04/02/creator-mode-art-tutorial/)

## Repeating the checks

The first command exercises the included, attributed 3D Hack source offline. The second also retrieves the publisher originals, full-size AV PNG and 7X editor screenshot into the selected directory. Use Python 3.9+ for corpus checks and Pillow 9.1+ for `--compare-art`. Replace the executable path with the build to test.

```sh
python3 tests/legacy/verify_corpus.py build/flame-cpu
python3 tests/legacy/verify_corpus.py build/flame-cpu --cache ./corpus-cache --fetch --compare-art
```

Once populated, the same cache works offline when `--fetch` is omitted. Archive and member SHA-256 hashes, extraction paths and source URLs are pinned in [the verifier](../tests/legacy/verify_corpus.py). A failed hash stops the comparison. Only the Wikimedia fixture is redistributed in this source package. Its [attribution](../tests/legacy/ATTRIBUTION.md) includes the source revision and license.
