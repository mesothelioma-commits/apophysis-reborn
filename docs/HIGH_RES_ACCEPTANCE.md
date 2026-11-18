# High-resolution acceptance — 0.7.0

This milestone was exercised with actual **16,384 × 16,384** rendering, full external decoding, exact strip-boundary comparisons, process termination and restart, and a native Qt queue. The input is the bundled Ember spiral. The full decoded dimensions contain **268,435,456 pixels** and **2,147,483,648 bytes** of RGBA16 channel data. The renderer sampled that full camera directly; no smaller image was enlarged.

The test uses 8,388,611 requested samples to exercise bounded storage, non-full final batches and complete output. That count is deliberately modest relative to 268 million pixels. The image remains sparse at native scale; this is functional, precision, storage and responsiveness acceptance, not a claim of finished artistic quality or convergence. Its included preview is a reduction of decoded full-resolution pixels, with no exposure adjustment.

## Executed 16K cases

All cases use CPU sampling, seed 123456, one-times oversampling, Gaussian radius 0.5, PNG16 straight RGBA, a 128 MiB renderer working-buffer budget and an 8192 MiB disk admission budget. All 8,388,611 samples plotted and invalid orbit steps were zero.

| Case | Camera scale | Strips / workers | Result | Wall time | Peak whole-process RSS |
| --- | ---: | --- | --- | ---: | ---: |
| CLI baseline, independently decoded | 4096 | 63 rows / 4 requested workers | Complete 16K file | 63.802 s | 40,180 KiB / 39.2 MiB |
| Native Qt queue, pause/resume | 5461.333333333333 | Automatic 247 rows, 67 strips / 2 workers | Complete 16K file after durable pause | 71.095 s process; 70.044 s render | 187,156 KiB / 182.8 MiB |
| CLI matched-camera comparison | 5461.333333333333 | 63 rows / 4 requested workers | Entire file identical to queue output | 70.572 s | 39,904 KiB / 39.0 MiB |

Timings are individual measurements on a shared Linux development host. They are not a hardware benchmark, a speedup measurement or a render-time guarantee. The initial baseline's CLI reported the last partial chunk's one worker; a later metadata fix retains the maximum worker count across chunks. Its invocation requested four, and the final test suite covers the corrected reporting.

The queue planned **134,145,481 bytes** of rendering buffers, below 128 MiB (134,217,728 bytes). Whole Qt process RSS includes the editor, previews, GUI/runtime libraries, stacks and allocator overhead. The host budget does not cap all process memory. The smaller fixed-strip CLI case naturally used substantially less than the budget. GPU driver allocations and OS filesystem caches are additional considerations; no physical GPU memory measurement is claimed.

A final planner review also added allowance for the old and new checkpoint manifests during atomic replacement, including escaped/quoted model metadata. A regression with a 300,000-character escaped flame name checks this storage bound. That conservative disk-allowance correction does not change sampling, strip pixels or the acceptance images.

## Full decoding, channel precision and file completeness

The original CLI baseline is **279,569,645 bytes** with SHA-256:

```text
6ba47d38e2fd3a9932378a6d23eaa3922240dc9908232a3a387316e4454651cb
```

A bounded independent Python reader validated all **9,415 PNG chunks**, each CRC, the complete deflate stream, every scanline, PNG unfiltering, the declared 16-bit RGBA layout and exact end-of-file. ImageMagick 7.1.1-43 Q16's external `stream` decoder independently read every full-resolution channel. Its native short output was normalized to big-endian RGBA16 before hashing. Neither route accepted only the header or a thumbnail.

Both decoders hashed all **2,147,483,648** decoded bytes and returned the same SHA-256:

```text
eede6bc849443ac0c04e697a113cb778770b03f320feda5df0adf6cdc4924e8d
```

| Channel / alpha property | Measured value |
| --- | ---: |
| Distinct red channel codes | 52,564 |
| Distinct green channel codes | 52,701 |
| Distinct blue channel codes | 46,824 |
| Distinct alpha codes in this sample | 227 |
| Zero-alpha pixels | 230,449,365 |
| Partial-alpha pixels | 30,410,278 |
| Full-alpha pixels | 7,575,813 |
| Zero-alpha pixels with nonblack RGB | 0 |

The 227 alpha codes reflect the sparse fixture, not an eight-bit alpha encoder. All 30,410,278 partial-alpha pixels use codes that are not eight-bit values multiplied by 257. Separate synthetic codec fixtures exercise a wider 16-bit alpha range and compare exact decoded samples. The color channels contain tens of thousands of distinct codes, demonstrating a path with more precision than expanded RGB8.

ImageMagick's full streaming decode took 20.46 seconds and peaked at 10,368 KiB RSS. Its ordinary full-image cache operation encountered the installed 2 GiB cache policy, so the external verification used the documented row-streaming decoder without relaxing that policy. A small fixture established exact agreement with ImageMagick's ordinary decoder after endian normalization.

Evidence in the distribution: `verification/high-resolution/full-16k-independent-verification.json`, the two decoder reports, scripts and logs. The included full baseline is `renders/ember-16k.png`. `preview/ember-16k-0.7.0.png` is an exact 16 × 16 box reduction after compositing straight RGBA over black and rounding to RGB8. The detail image is a native 1:1 crop at (7680, 7680), not an enlargement.

## Strip boundaries and pause/resume equality

The initial CLI and queue acceptance fixtures used different camera fits. CLI `--size 16384 16384` multiplies the original scale by the smaller dimension ratio, producing scale 4096; the explicit large-queue smoke fixture uses 16384/576 and scale 5461.333333333333. That setup mismatch was identified before making any seam claim.

A derivative flame was then prepared with the queue's exact camera. Its complete effective XML model was compared against the queued snapshot after the same oversampling/filter overrides. Rendering that model with **63-row strips** and four requested CPU workers produced the same **359,217,798 PNG bytes** as the queue's **247-row strips**, two CPU workers and a pause/resume after 65,536 committed samples. Both files have SHA-256:

```text
6a1783f4cf3495fdc1b215ee934ec9badbc82aa9ba9fe13a24abc21625fd9ef1
```

Equality covers the complete image and every strip boundary; it is stronger than visually checking a few seams. Small deterministic tests separately compare the earlier dense renderer with streamed reconstruction in **30 CPU cases** and **six Vulkan cases**, including generic and historical math, box and Gaussian filters, narrow odd/even footprints, rotation, opacity, pre-blur and final transforms. The baseline used for independent full decoding and the matched-camera files are separate evidence, identified in their records.

Reproduction settings, effective-model comparison and complete-file equality are retained in `verification/high-resolution/full-16k-strip-queue-comparison.json`, `matched-camera-model-verification.json`, the derivative flame and render log. The two matching large files are not both duplicated in the distribution.

## Queue responsiveness, pause and desktop interaction

The optional `--large-render-smoke-test DIR` ran the real `MainWindow`, visible `RenderQueueDialog` widgets and actual queue worker. Its report explicitly identifies **Qt's offscreen platform**. It is an automated native-widget check, separate from on-screen interaction.

| Queue observation | Result |
| --- | ---: |
| Samples durably committed at pause | 65,536 |
| Stop latency after pause request | 5 ms |
| Existing destination preserved while paused | Yes |
| Checkpoint validated before reporting paused | Yes |
| Resumed sampling reported | Yes |
| UI timer interval | 10 ms |
| UI timer callbacks while active | 6,972 |
| Largest observed callback gap | 98 ms |
| Completed reconstruction strips | 67 / 67 |

The largest gap was below the mode's one-second failure threshold. The accepted large run preceded final additions to persisted worker-count/progress/phase-ETA metadata; sampling and pixels were unchanged. Actual workers are independently retained in the core manifest evidence. The final source passes the complete queue test suite and the small smoke, which exports PNG16 RGBA and TIFF16 RGB through two real jobs.

A separate manual check used the **actual XCB Linux desktop**. It opened the render settings, inspected all eight presets through 16K, selected PNG16/straight alpha and a 128 MiB budget, and observed the strip plan. It then entered 512 × 512 and 1,000,000 samples, added a stopped job, explicitly started it and observed CPU sampling followed by Complete at 100%. The resulting PNG's header and complete Pillow decode confirmed 512 × 512 RGBA16. The desktop record retains the tested binary hash and output hash; the final metadata changes are additionally covered by the frozen-source smoke.

The queue tests also cover an immediate pause from the worker-start signal, cancellation and destination preservation, failure retry, immutable snapshots, exact 64-bit settings, source math flags, queue ordering, restore rollback, fresh start-over identities and destruction into a stopped resumable queue. The unchanged renderer-details layout regression passes with all controls visible.

## Actual process termination and restart

An independent harness launched a 768 × 512 PNG16 RGBA render with 4,000,003 samples, oversampling two, filter radius 0.4, strip height 19, 64 MiB host budget and a persistent checkpoint. After **65,536** samples were durably committed it sent **SIGKILL (9)**. The killed process returned -9, and an existing destination retained its original bytes.

A separate process resumed the checkpoint with three workers. A fresh uninterrupted render used one worker. Both completed with identical counters and identical 858,174-byte PNGs, SHA-256:

```text
2b2ff3638874960602c15edf8587f2ab962ffcf52343ad3fcdb094b35ff85b51
```

A separate actual SIGINT test returned 130, preserved the old destination and retained a usable checkpoint. Unit and CLI checks additionally cover missing/truncated/corrupt chunks, corrupted manifests, mismatched model/settings/device/shader identity, uncommitted orphan chunks, simultaneous writers, reconstruction cancellation and fully sampled checkpoint reconstruction. These results validate the tested process-interruption cases; they do not claim tolerance of arbitrary filesystem or hardware failure.

Evidence: `verification/restart/verification.json`, `sigint-verification.json`, harness, retained manifest and process logs.

## Boundaries and reproduction

The full 16K execution was CPU-based. Vulkan file sampling and dense/stream equality executed on **llvmpipe software Vulkan**, including absolute chain ranges, partial chains, 16K global coordinates under bounded buffers and GPU checkpoint resume. This host exposes no physical GPU. NVIDIA/AMD/Intel execution, Windows 11 UI/DPI behavior and physical GPU performance remain pending. The Windows executables were compiled and audited, not executed.

Run the ordinary source suite with `bash scripts/build-editor.sh`. The full 16K queue mode is separate because it writes hundreds of megabytes of compressed image data and retains sampled checkpoints. On Windows, `bash scripts/verify-editor-windows.sh --large-render` opts into it. The ordinary verification script does not require that large job. [Build and platform workflow](QT_BUILD.md) · [Strict GPU workflow](WINDOWS_GPU.md)

The full checkpoint history, exact verification reports and distribution checks are included in the ZIP. Read [HIGH_RES_RENDERING.md](HIGH_RES_RENDERING.md) for practical commands and storage cleanup, [RENDER_QUEUE.md](RENDER_QUEUE.md) for lifecycle semantics, and [IMAGE_FORMATS.md](IMAGE_FORMATS.md) for color/alpha and encoder limitations. JPEG, floating-point OpenEXR, ICC/profile color management and dithering remain open; operating-system shutdown after a queue is not implemented.
