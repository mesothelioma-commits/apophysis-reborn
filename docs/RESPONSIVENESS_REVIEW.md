# Responsiveness and rendering review — 0.10.1

## Scope and conclusions

This maintenance work follows a report that versions 0.8 and 0.9 sometimes froze during ordinary use without a clear trigger. The audit started from the preserved 0.10.0 release source and executable. It identified a reproducible preview state bug, synchronous GUI work that could stall the editor, unnecessary render-queue event/persistence work, and output-stage rendering overhead.

These findings justify the fixes below. They do not establish which path caused every freeze on the reporting machine. Native Windows execution and a physical graphics adapter were not available in this environment; software Vulkan and a Linux Qt desktop exercise different platform components.

## Preview completion and cancellation

Qt direct signal connections can run application code before the emitting function returns. Previously, the preview controller checked a result generation only once, then emitted several signals. A listener could stop, replace or destroy the controller during the first signal, after which the old callback could publish stale state or an image. Request invalidation could similarly be interrupted and leave the busy flag set after Stop.

The new `reentrantStopDoesNotLeaveBusyState` test fails when linked to the untouched 0.10.0 libraries and matching original headers: the controller remains busy after the stop. The isolated baseline build, hashes and failure log are retained as release evidence. This is a deterministic state regression, not a reproduction of an operating-system hang.

The preserved 0.8.0 and 0.9.0 source tags contain the same request/invalidation ordering. Their exact commits and request method excerpts are retained in `historical-preview-source.json`; those historical executables were not rerun as part of this investigation.

The controller now checks lifetime and request generation after callbacks that can reenter it. One immutable input snapshot is shared through debounce/restart scheduling. Model validation and fitting happen on the worker. Undelivered progress is replaced by the latest value rather than accumulating GUI events. Tests cover replacement, stopping, stale errors, destruction during completion, snapshot ownership, queued progress and repeated edits/pause/restart with an event-loop heartbeat.

Relevant code: `src/editor/preview_controller.cpp`; regressions: `tests/preview_tests.cpp`.

## Work removed from the interface thread

### Device discovery

Opening renderer controls previously called `compute_devices()` synchronously, including when CPU rendering was selected. Driver initialization/enumeration can take substantial time or stall inside a vendor library. A UI-thread probe therefore prevented unrelated editor interactions.

CPU-only controls now avoid discovery. GPU-capable modes request one background probe, poll an owned result snapshot, and reuse a cached inventory. An explicit Refresh action repeats discovery after a completed probe. A dialog can close while discovery is pending; the detached probe owns pure shared state and never touches a widget or joins on dialog destruction. The injected blocked-probe regression checks UI heartbeat, bounded repeated requests and safe dialog destruction.

Relevant code: `src/editor/device_discovery.hpp`, `src/editor/main_window.cpp`; regressions: `tests/editor_tests.cpp`.

### Periodic recovery

The 45-second recovery timer previously prepared the complete project, encoded a preview thumbnail, serialized it and performed disk I/O on the GUI thread. That timing is a plausible reason a freeze can appear unrelated to the most recent action.

The timer now captures an owned snapshot and submits it to one recovery worker. Thumbnail encoding, project preparation, serialization and atomic writes run there. There is one active operation and at most one pending replacement. Opening another document or discarding recovery increments a generation and retires the old path; obsolete work cannot become the recovery for a new document. A worker holds its recovery-file lease until cleanup or shutdown. Explicit synchronous recovery remains available to callers that require completion before returning.

Snapshot capture still copies current document bytes on the GUI thread. It avoids copying and immediately discarding a second cached library, and shares the retained original project source. This is a bounded snapshot handoff, not a claim that opening, editing or explicitly saving arbitrarily large documents is instantaneous.

Relevant code: `src/editor/document_session.cpp`, `src/editor/main_window_extensions.cpp`; regressions: `tests/document_session_tests.cpp`.

## Render queue and resource use

The queue formerly posted a GUI callback for every render progress update and serialized/wrote all jobs approximately once per second. One tall image with tiny strips could produce thousands of notifications; large animation queues amplified persistence and widget refresh costs.

The worker now publishes one latest-progress snapshot, read by a 50 ms GUI timer. Queue writes occur at durable state transitions instead of cosmetic progress updates. Independent render checkpoints retain resume safety. A persistent worker handles successive jobs, so thread-local Vulkan context/pipeline resources can be reused across frames. Pause and Cancel request cooperative cancellation; explicit `pauseAndWait()` waits for that job’s completion rather than joining a thread intended to remain alive.

Regression coverage includes a 2,048-strip backlog, final sample/strip totals, output preservation, pause/restore/resume equivalence, nested pause calls, and reusing a job ID after completion. Whole-queue admission, explicit save and final durable state writes remain synchronous operations; this change specifically removes repeated progress-triggered persistence.

The dense CPU renderer also now budgets its transform-selection distributions and opacity lookup before allocating worker buffers. Default/all-one chaos rows continue sharing the initial distribution. Models with many nondefault reachable chaos rows can require quadratic table storage; a model over the configured working budget now fails admission instead of allocating those tables outside the budget. Streamed file planning already included a conservative model/chaos allowance.

## Renderer optimization method

The initial investigation separated sampling from reconstruction and file writing. A private instrumented baseline build timed row development, output conversion/filtering, compression/write, checkpoint reading and allocation. The original release executable and source remained unchanged. The instrumented 16K PNG matched the original encoded file exactly. The instrumentation is diagnostic evidence; its total time must not be mixed with uninstrumented benchmark totals.

Clock-based profiling through gprofng was attempted, but its collector reported that the interval timer was changed to zero. No sample percentages from that unreliable recording are used.

The implemented optimizations target measured work:

- Precompute the defined zero-light output and skip repeated gamma/channel calculations for those pixels. Preserve historical 8-bit background flooring, modern rounding, transparent zeros, active tone-curve semantics and zero-brightness behavior.
- Select the writer’s format/depth outside the per-pixel loop, use equivalent positive-value quantization, and reuse encoded bytes for identical adjacent pixels.
- Retain PNG compression, filtering order, channel depth, sample count, deterministic sampling and image quality settings.

The CPU sampler already distributes independent sample batches and deterministically merges results. Image reconstruction and PNG compression still contain serial work. Higher CPU utilization alone is not evidence of faster output; paired timings and exact image checks determine whether an optimization is useful. The defaults continue leaving one hardware thread available for the interface during automatic CPU previews, within the renderer’s memory and batch limits.

### Measured results

On the instrumented baseline, developing the 16K rows took 16.73 s. Output took 24.60 s, including 5.42 s for quantization, 1.98 s for filtering and 17.19 s for compression/write. Separate uninstrumented CPU comparisons retained the same Ember scene, 2,097,152 samples, seed 123456, four workers, precision and compression. Each large workload had an excluded warmup pair and three measured pairs with alternating order.

| File workload | 0.10.0 median elapsed | Optimized candidate median elapsed | Interpretation |
| --- | ---: | ---: | --- |
| 2,048 × 2,048 RGBA16 PNG | 2.301 s | 1.908 s | 17.1% lower elapsed time; exact encoded output |
| 16,384 × 16,384 RGBA16 PNG | 45.560 s | 40.700 s | 10.7% lower elapsed time; all three matched pairs improved; exact encoded output |
| 512 × 384 RGB8 PNG | 0.972 s | 1.120 s | Mixed; no short-render acceleration claim |

For RGB8, the first candidate’s repeated-pixel cache added overhead that was not clearly beneficial. The final implementation writes 8-bit pixels directly and keeps the cache for 16-bit output. The follow-up used all six orders of baseline, original candidate and revised candidate, repeated twice (12 measured runs of each after warmup). The revised candidate was faster than the baseline in 9 of 12 matched rounds, yet had the higher individual median shown above. Sampling and elapsed times varied substantially. The measured reconstruction medians were 0.310 s and 0.303 s; the whole-render result is inconclusive. All 39 warmup/measured RGB8 files matched byte-for-byte. These numbers describe this host and workload, not a general speedup for every flame or GPU.

The 16K encoded SHA-256 remained `95103c28941b4ba8ab0dc5d98153ea03e9e92011de44716c4fe29c6d76c6c6e3`. The larger timings used an immutable interim candidate; the 8-bit follow-up used a second immutable candidate with the 16-bit path unchanged. The verification package fingerprints both candidates and compares relevant source files with the final release. Final release executable checks are recorded separately.

## Remaining memory risk

Library undo commands still retain complete before/after XML strings. The 100-command cap bounds the count rather than the number of bytes. With an unusually large 64 MiB library, 100 such commands could retain about 12.5 GiB in command payload alone. That is a calculated upper-end scenario, not a measurement or a diagnosis of the reported freezes. Reversible byte splices and a byte-limited history that preserves dirty/clean semantics are a separate follow-up.

## Cancellation and hardware limits

The audit did not add unsafe thread termination or destroy resources that a submitted GPU command may still use. Vulkan work must complete before its buffers can be freed; a fence timeout reports pending work rather than cancelling it. The existing renderer uses bounded submissions and cooperative cancellation between them. A native plugin or graphics driver that never returns cannot be safely terminated in-process by this patch. Process isolation would be a separate architectural change.

API references: [Qt threads and QObjects](https://doc.qt.io/qt-6.8/threads-qobject.html), [Vulkan fence waits](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForFences.html), [Vulkan buffer lifetime](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyBuffer.html).

## Verification evidence

The distribution’s verification records bind the final source and executable identities to executed tests and paired render measurements. Baseline reproduction, focused regressions, full native tests, sanitizer checks, platform build/audit results and any unexecuted platform gates are reported separately. Historical results remain labeled with their original version.
