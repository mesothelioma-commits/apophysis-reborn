# Historical reference checks

The numerical baseline is the FLAM3 3.1.1 renderer at upstream commit `1cdfc77275821182ad3c057e629c75a240dd52d3`. Its transform code also appears in Debian source `3.1.1+ds2-2`. The project runs its own C++ engine for each comparison. It does not link FLAM3 into the product.

## Transform vectors

The upstream C functions for variation IDs 0–14 and `apply_xform` produced the 126 rows in `tests/reference-vectors.tsv`. Seven input points exercise all four quadrants, coordinate axes, and a point close to the origin. Julia has an additional result for the alternate random bit. Two other sets of inputs exercise an imported spherical transform from the upstream example and a signed mixture of three variations between two asymmetric affine matrices. The comparison includes the updated color coordinate. Each coordinate must agree to within `1e-12 * max(1, abs(reference))`.

The generator isolates the original upstream C function bodies and controls the one Julia branch bit. A fingerprint checks the operations before compilation. Only the generated numbers, the fixture and the generation harness are included here. To reproduce with Python 3 and a GNU-compatible C compiler on Linux, run from the source package root:

```bash
git clone https://github.com/scottdraves/flam3.git /tmp/flam3-source
git -C /tmp/flam3-source checkout --detach 1cdfc77275821182ad3c057e629c75a240dd52d3
python3 tests/reference_generate.py /tmp/flam3-source /tmp/regenerated-vectors.tsv
diff -u tests/reference-vectors.tsv /tmp/regenerated-vectors.tsv
```

Rebuilding the vectors from that checkout gave identical bytes with GCC 14.2.0 and glibc 2.41 on x86-64. Last digits can depend on the platform's math library. The checked-in vector SHA-256 is `386a7dffdaea29f296602f26de8e776f1f9dca989824935008e7ae4aea619268`.

## Whole-image comparison

`tests/reference-image.flame` uses the four affine maps, weights, color coordinates and spherical variations in the first flame of upstream `test.flam3`. The fixture sets a 192 × 144 canvas, a proportional camera scale of 72 pixels per unit and a new, explicit 256-entry palette. Both programs read the same XML. The complete upstream reference executable rendered a PPM with 64-bit buckets and one worker. Rebuilding the reference under both GCC 14.2 `-O2` and the upstream `-O3 -ffast-math` flags produced identical output. Its SHA-256 is `a78ef8bbcbf824c4e7fd141b8a9d6c0bf399ce1a676413c29563ec0c85e508d9`. The Reborn CPU executable requested 3,317,760 samples, one worker and seed 27. The seed controls different generators in the two programs.

| Metric over the entire unscaled RGB8 raster | Observed | Regression limit for this fixture |
| --- | ---: | ---: |
| Mean absolute difference across RGB channels, 0–255 scale | 4.832694 | ≤ 6.5 |
| Root mean squared difference across RGB channels, 0–255 scale | 6.465777 | Reported |
| Pearson correlation of pixel intensities, defined here as (R+G+B)/3 | 0.984265 | ≥ 0.975 |

The test includes every pixel, without cropping, aligning, smoothing or normalizing the two images. FLAM3 uses ISAAC, its own transform-selection tables, density estimation, reconstruction filter and tone processing. Reborn uses seeded batches, fixed-point accumulation and box reconstruction. Differences among these stages all contribute to the measurements. This fixture covers FLAM3's static spherical geometry on this view and palette. Apophysis 2.x, 7X and AV binaries need separate comparisons.

To reproduce the reference after building the above checkout, run these commands from the Reborn source package root. Adjust the executable path to the location of your built FLAM3. The ordinary CTest run uses the checked-in reference PPM and requires only the Reborn build.

```bash
nthreads=1 seed=27 isaac_seed=27 bits=64 verbose=0 format=ppm \
  in="$PWD/tests/reference-image.flame" out=/tmp/reference-flam3.ppm \
  /tmp/flam3-source/flam3-render
./build/flame-cpu --flame tests/reference-image.flame --threads 1 --seed 27 \
  --output /tmp/reference-reborn.ppm
./build/reference-tests tests/reference-vectors.tsv tests/reference-geometry.flame \
  tests/reference-image.flame /tmp/reference-flam3.ppm
```

## Source map and attribution

| Primary source at the pinned revision | Use |
| --- | --- |
| [variations.c](https://github.com/scottdraves/flam3/blob/1cdfc77275821182ad3c057e629c75a240dd52d3/variations.c) and [private.h](https://github.com/scottdraves/flam3/blob/1cdfc77275821182ad3c057e629c75a240dd52d3/private.h) | Variation formulas, precomputation, numerical epsilon, affine and post-affine order, color propagation |
| [parser.c](https://github.com/scottdraves/flam3/blob/1cdfc77275821182ad3c057e629c75a240dd52d3/parser.c) and [flam3.c](https://github.com/scottdraves/flam3/blob/1cdfc77275821182ad3c057e629c75a240dd52d3/flam3.c) | XML coefficient interpretation, symmetry-to-color-speed conversion, selection weights and final-transform behavior |
| [rect.c](https://github.com/scottdraves/flam3/blob/1cdfc77275821182ad3c057e629c75a240dd52d3/rect.c) | Camera orientation, rotation, palette lookup, accumulation and reconstruction |
| [test.flam3](https://github.com/scottdraves/flam3/blob/1cdfc77275821182ad3c057e629c75a240dd52d3/test.flam3) | Historical geometry used in the two adapted fixtures |
| [Debian Sources mirror](https://sources.debian.org/src/flam3/3.1.1%2Bds2-2/) | Independently accessible copy of the numerical baseline |

FLAM3 credits Spotworks LLC, copyright 1992–2009, and is distributed under GPL version 3 or later. Its license accompanies the geometry adapted from the upstream fixture, in `tests/reference-FLAM3-LICENSE.txt`.
