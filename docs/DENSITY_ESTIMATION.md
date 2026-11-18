# Density estimation in 0.12.0

Open `examples/density-study.flame` and select **Tidal filaments**. In **Adjust → Rendering**, turn **Enable density estimation** on or off while keeping the seed and sample count fixed. Sparse parts of the image spread their light across a wider area. In dense areas, the estimator reduces the radius to retain detail.

| Control | Meaning |
| --- | --- |
| Maximum radius | Smoothing radius in output pixels for a sparsely hit region. The example uses 3.5. |
| Minimum radius | Lower bound as hit density increases. The example uses 0.25. |
| Estimator curve | Positive exponent that controls how quickly the radius decreases with density. The example uses 0.4. |

The FLAM3 3.1.1 constructors default to radius 9, minimum 0 and curve 0.4. A radius of zero disables estimation. Equal positive maximum and minimum radii take the original renderer’s single-filter bypass. For an unversioned or Reborn flame, an explicit positive radius enables this feature. Tagged FLAM3 3.1.1 flames use the verified producer defaults when the attributes are absent. Source-preserving edits and canonical exports retain an explicit disabled value when needed.

The pinned Apophysis 7X 15D internal image maker skips estimator initialization even when its saved `enable_de` flag is 1. Reborn honors that behavior for its identified producer tags and retains the original parameters on source-preserving saves. Active estimators specific to other historical producers require their own evidence. [7X image maker](https://github.com/xyrus02/apophysis-7x/blob/1cefa4099927e296d365412bd0b91b96f85606ca/src/Rendering/ImageMaker.pas)

## Rendering and memory

The estimator counts local hits on the oversampled histogram, selects a normalized Gaussian kernel, and distributes each occupied bucket’s log-exposed color and opacity. Spatial reconstruction and gamma processing then produce output pixels. The renderer reserves a margin around the camera and overlapping input rows for streamed rendering, so density smoothing can cross strips. Both CPU sampling and Vulkan sampling feed the host-side estimator. The file-render planner includes kernel tables, histogram margins and reconstruction storage in the host-memory budget. Large radii, high oversampling and small curves can make a job exceed its budget. [FLAM3 kernel construction](https://github.com/scottdraves/flam3/blob/1cdfc77275821182ad3c057e629c75a240dd52d3/filters.c) · [FLAM3 density pass](https://github.com/scottdraves/flam3/blob/1cdfc77275821182ad3c057e629c75a240dd52d3/rect.c)

## Reference comparisons

| Comparison | Result |
| --- | --- |
| Original FLAM3 64-bit filter and density worker, supplied five 39 × 33 histograms at oversampling 1–5 | All 25,740 linear-light RGBA values met the comparison tolerance of `1e-12 × max(1, abs(reference))`. Includes overlapping colors, neighborhood counts above 100, clamping, boundary clipping and even-oversample adjustment. |
| Complete upstream FLAM3 versus Reborn CPU, 32 × 24 fixed-position flame, 96,000 samples | Maximum difference of one 8-bit level over all 2,304 RGB channels. The same comparison passed with strict Vulkan sampling on Mesa llvmpipe (software Vulkan). |
| Complete upstream FLAM3 versus Reborn CPU, **Tidal filaments**, 384 × 256, 983,040 samples, one worker, seed 27 | Mean absolute difference 3.86 levels per 8-bit RGB channel, root-mean-square difference 8.12. Every channel is included at its original location. The programs use different orbit random-number generators and quantization. |
| Original 7X Pascal image maker with the estimator flag both enabled and disabled | All five 8 × 6 controlled images and their original Gaussian coefficients agreed byte-for-byte. |

The regression suite also compares dense reconstruction with one- and three-row CPU file renders, checks that cancellation retains the prior output, and exercises XML defaults and edit round trips. Physical GPU driver results and Windows installer execution are separate platform checks.

## Regenerating the original references

Use FLAM3 source commit `1cdfc77275821182ad3c057e629c75a240dd52d3`. The generator checks source fingerprints, compiles the upstream kernel construction and density worker for double-precision buckets, and supplies deterministic histogram inputs. The ordinary CTest run uses the checked-in vectors and original impulse image.

```bash
python3 tests/legacy/density-oracle.py /path/to/flam3 /tmp/density-oracle.tsv
cmp /tmp/density-oracle.tsv tests/legacy/density-oracle.tsv

nthreads=1 seed=27 isaac_seed=27 bits=64 verbose=0 format=ppm \
  in="$PWD/tests/legacy/density-impulse.flame" out=/tmp/flam3-density-impulse.ppm \
  /path/to/flam3/flam3-render
cmp /tmp/flam3-density-impulse.ppm tests/legacy/density-impulse.ppm

ctest --test-dir build -R 'density_estimation|producer_pixels' --output-on-failure
```

For the artistic comparison, use `examples/density-study.flame` in the upstream render command and render that same file with `flame-cpu --threads 1 --seed 27`. For the 7X comparison, run the [original Pascal harness](PRODUCER_ORACLE.md) with the pinned 7X tree. It renders each controlled case twice and compares the images and filter coefficients.
