# Animation model and source preservation

M9 adds a versioned, deterministic timeline to native Reborn projects. A timeline contains immutable snapshots of complete legacy flame collections, their selected entries, explicit transform correspondence IDs, frame positions and interpolation settings. Preview and sequence export evaluate the same `PreparedAnimation` model.

This is a Reborn timeline format. It does not claim compatibility with arbitrary Apophysis AV binary projects, undocumented animation payloads, or a video encoder. Legacy `time` metadata can be imported with an explicit choice of units; it is not interpreted as a universal historical frame number.

## Native project boundary

`ProjectFile.animation_data` remains an optional opaque byte string with an 8 MiB limit. The project envelope preserves it without loading plug-ins or interpreting its contents. The animation module recognizes and evaluates the following UTF-8 XML schema:

```xml
<apophysis-animation version="1" name="Example"
    fps-num="30000" fps-den="1001" frame-count="121"
    loop="once" affine="polar" palette="rgb" camera-angle="shortest"
    topology="strict" seed="123456" seed-mode="fixed">
  <keyframe frame="0" name="Start" easing="smoothstep" plugin-identity="">
    <snapshot encoding="base64" selected="1">BASE64_OF_COMPLETE_SOURCE_COLLECTION</snapshot>
    <transform id="trunk"/>
    <transform id="branch"/>
  </keyframe>
</apophysis-animation>
```

Every shown attribute is required, even when it takes a default value. Element order within a keyframe is immaterial except that the order of `transform` elements identifies source transform order. A keyframe has exactly one snapshot. The parser rejects unknown attributes/elements, duplicate snapshot elements, invalid numbers, invalid base64, duplicate transform IDs and unsupported versions. It never treats an unfamiliar field as an instruction to approximate a variation or enable a module.

`parse_animation` and `serialize_animation` validate envelope structure and legacy source XML. `PreparedAnimation` additionally validates the selected models and their ability to form each interpolated segment. Separating these operations allows an archival project containing unavailable plug-ins or unsupported future flame fields to be read and preserved without executing native code.

An unknown animation version or schema must remain in `ProjectFile.animation_data` until the user explicitly replaces it. Failure to evaluate it is not permission to remove it. Saving other project settings preserves the original opaque payload. Known payloads can be edited and serialized canonically; their envelope whitespace and attribute spelling are not archival data. Their embedded legacy source bytes are archival data and remain exact.

## Version 2 property keys

Adding the first property key upgrades a version 1 animation payload to version 2. A snapshot-only version 1 clip can still be opened, evaluated and saved. Older readers can preserve an unfamiliar payload in the outer project.

A version 2 timeline uses the same snapshots and accepts additional root children such as the following.

```xml
<property-key property="rotation" frame="0" value="0" easing="linear"/>
<property-key property="rotation" frame="120" value="720" easing="hold"/>
```

Each of the five tracks (zoom, rotation, brightness, gamma and vibrancy) can contain up to 256 strictly increasing, unique frame positions within the clip. The outgoing easing names are `hold`, `linear`, `smoothstep` and `smootherstep`. After snapshot evaluation, each active track overrides its own property, including on exact snapshot frames. Values hold before the first and after the last property key. Rotations interpolate in unwrapped degrees.

## Exact snapshots and endpoint models

`AnimationKeyframe.library_bytes` contains the complete original collection, not a canonical re-export of the selected flame. `selected_index` identifies its zero-based direct child entry. Base64 retains UTF-8, UTF-16LE/BE, Windows-1252 or other already supported legacy encodings, BOMs, comments, unknown sibling data, quote styles and whitespace. A keyframe retains an unknown sibling even if that sibling cannot render. Unsupported fields in the selected flame still prevent evaluation with a specific diagnostic.

Before applying property tracks, evaluation at an exact snapshot frame copies the parsed source model exactly. It does not rebuild affine coefficients, normalize angles, reorder transforms, expand Xaos rows, change palettes or apply interpolation to that endpoint. After the final keyframe, the last model holds until the last exportable frame. A hold segment also returns the unmodified left model until the next keyframe.

The immutable prepared model owns any native variation modules needed by those snapshots. Disabling a plug-in after preparation prevents new evaluation contexts from acquiring it but does not invalidate an already prepared preview or sequence. The snapshot's `plugin_identity` records the sorted variation name, module ID, module version and actual binary SHA-256 for every used native variation. Preparation requires an exact match with the explicitly enabled module. A changed module or a build for another platform requires deliberate re-capture; a same-name or same-version replacement is insufficient.

## Timeline arithmetic

| Field | Definition |
| --- | --- |
| `frame-count` | Number of exportable frames, from 0 through `frame-count - 1` inclusive |
| `fps-num / fps-den` | Rational frame rate, such as 30000/1001; no accumulated floating-point playback counter |
| Frame timestamp | `frame × fps-den / fps-num` seconds |
| Clip duration | `frame-count × fps-den / fps-num` seconds, including the last frame's display interval |
| Keyframe span | Difference of integer frame positions, distinct from clip duration |
| `once` | Clamp an external playback position to the first/last exportable frame |
| `repeat` | Modulo `frame-count`, including the last frame once per cycle |
| `ping-pong` | Period `2 × (frame-count - 1)`, without duplicated turnaround endpoints |

For a five-frame clip, ping-pong playback maps to `0,1,2,3,4,3,2,1,0,1,...`. Negative playback positions wrap consistently; a single-frame clip always maps to zero. The integer mapping handles the full signed 64-bit input range, including its minimum value. Sequence export uses the requested inclusive range inside `0..frame-count-1`; looping controls playback and arbitrary evaluation positions, not an implicit infinite export.

The first keyframe must be at frame zero. Later keyframes must be strictly increasing, distinct and inside the clip. There is no silent sorting or retiming. When two keyframes are adjacent, there are no integer interior samples, so they constitute an exact cut even if their producers or topology differ.

The outgoing easing belongs to the left keyframe. For normalized segment position `u`:

| Easing | Interior position used by interpolation |
| --- | --- |
| `hold` | 0 until the right endpoint |
| `linear` | `u` |
| `smoothstep` | `u²(3 - 2u)` |
| `smootherstep` | `u³(6u² - 15u + 10)` |

Exact endpoints bypass these formulas and return the source model. Flame `time` remains source metadata and interpolates as a real number; it does not replace the clip frame number or timestamp.

## Deterministic seeds

The seed is an unsigned 64-bit integer represented as decimal text without conversion through a JSON number or a floating-point UI value. Fixed mode uses it on every frame. Per-frame mode computes a portable SplitMix64 output from the mapped clip frame:

```text
x = seed + 0x9e3779b97f4a7c15 × (frame + 1)
x = (x XOR (x >> 30)) × 0xbf58476d1ce4e5b9
x = (x XOR (x >> 27)) × 0x94d049bb133111eb
result = x XOR (x >> 31)
```

All arithmetic wraps modulo 2^64. A seed of zero yields `e220a8397b1dcdaf` for frame 0 and `6e789e6aa1b965f4` for frame 1. Repeated playback of one mapped frame receives the same seed. This establishes repeatable inputs; renderer backend, build, sampling settings, variation order and native plug-in behavior still determine pixel-level reproducibility. A fixed seed does not guarantee that changing a flame will retain identical noise patterns.

## Validation before preview and export

1. `parse_animation` checks the byte-bounded schema and all source XML without enabling native code.
2. `PreparedAnimation` checks every selected keyframe, correspondence map, producer family, curve mode and native identity, and reports fallback/discrete-setting diagnostics.
3. `validateFrames(first, last, stop)` evaluates and validates every selected output frame before sequence publication. Valid endpoints alone are insufficient: separately interpolated camera scale and zoom, for example, can overflow their effective product between finite endpoints.
4. The renderer applies its normal backend, sample-count, dimensions, memory and output checks to the evaluated models. Native callbacks are executed during rendering, not during metadata validation; their runtime failures remain possible and are reported by the render job.

Preparation and range validation accept a stop token and throw `AnimationCancelled`. Evaluation itself is synchronous and bounded by the selected flame's model. No output file is created by the animation core. The sequence host owns output preflight, immutable render settings, checkpoint identity, file publication and resume policy; see [ANIMATION_CLI.md](ANIMATION_CLI.md).

## Resource and input limits

| Resource | Limit |
| --- | --- |
| Animation payload | 8 MiB actual encoded XML |
| Combined decoded source snapshots | 6 MiB, including repeated full collections |
| Keyframes | 1–256 |
| Ordinary transforms per keyframe | 1–256 |
| Ordinary transform union in an interpolated segment | At most 256 |
| Variation terms per endpoint transform | At most 128, including a final transform |
| Exportable frames | 1–1,000,000 |
| Frame rate | 1–240 fps; numerator at most 240,000 and denominator at most 10,000 |
| Clip/keyframe name | At most 1,024 UTF-8 bytes |
| Native identity record per keyframe | At most 64 KiB |
| Correspondence ID | 1–64 ASCII letters, digits, `.`, `_`, `-`; `__final` is reserved |
| XML | Depth at most 4, at most 100,000 elements, at most 20 attributes per element |

DOCTYPE declarations are rejected. No external entities, file paths, includes or module installers exist in this schema. Base64 padding is canonical; XML whitespace inside base64 is allowed. Labels must be valid XML-compatible UTF-8. The actual 8 MiB encoded-size check remains necessary because a 6 MiB snapshot plus envelope overhead exceeds it. A large collection captured repeatedly consumes the snapshot budget repeatedly; M9 does not implement content-addressed snapshot deduplication.

## Presets and legacy time import

`make_animation_preset` supplies a still clip, a full 360° view rotation, a one-stop zoom and a palette cycle. Camera orbit uses unwrapped degrees and repeat playback; zoom adds one to `zoom` and uses ping-pong playback. The palette cycle captures up to four quarter-turn palette rotations and returns to the starting palette, with crossfades between captured palettes. A one-frame request produces a valid still frame. A short two-frame palette cycle has no interior sample in which to show movement.

`make_morph_animation` captures two original source documents. Empty maps explicitly request positional pairing and are accepted only for equal ordinary transform counts. Arbitrary topology changes require maps on both endpoints and the appropriate topology mode. See [MORPHING.md](MORPHING.md).

`animation_from_timed_library` requires the caller to select `frames` or `seconds`. Seconds are multiplied by the specified rational frame rate; positions round to the nearest integer frame with `std::llround`. It rejects negative/nonfinite/out-of-range times, duplicate rounded frames, unsorted times, a missing frame-zero start and differing transform counts. It preserves each original collection and selected index. This importer is a deliberate conversion utility, not a claim that every historical `time` field has the same unit.

## Regression coverage

`animation-tests` covers exact UTF-8/UTF-16 collection round trips, unknown-version project preservation, malformed and oversized envelopes, canonical base64 padding, rational timing, negative loop positions, seed vectors, endpoint equality, affine and camera interpolation, palette spaces, curve-mode boundaries, explicit correspondence and reordered Xaos, unreachable zero-weight branches, reflected/singular matrices, hard cuts, native real/integer parameters, module lifetime and fingerprint mismatches, preset generation and legacy time conversion. It also proves that valid camera endpoints can fail an interior-frame preflight.
