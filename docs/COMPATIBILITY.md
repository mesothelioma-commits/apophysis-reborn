# Static flame XML compatibility

This milestone reads static, two-dimensional transform geometry and embedded RGB palettes. The mathematical reference is FLAM3. A `version` string records the producer. It does not switch the renderer into an Apophysis 2.x, 7X or AV compatibility mode. No such version-specific mode has passed acceptance.

## Accepted input

| Area | Syntax and behavior | Verification |
| --- | --- | --- |
| Document | One `<flame>` or multiple direct `<flame>` children of `<flames>`. The collection may have a display name. One invalid flame fails the whole load. | Single, multiple, empty and invalid-sibling tests |
| Identification | Flame `name`, producer `version` and numeric `time`. XML attribute entities are decoded. Time is metadata for each static frame. | Metadata and quote/ampersand round trip |
| Camera | `size` (width height), `center` (x y), `scale`, `rotate` in degrees, and `zoom`. Center is also the rotation pivot in the FLAM3 reader. | Parsed camera/affine assertions; see render tests for projection |
| Sampling and tone | `quality`, `oversample` or its alias `supersample`, `background` channels in [0,1], `brightness`, `gamma`, `vibrancy`, and `palette_mode` of `step` or `linear`. XML import preserves these model values. The renderer's tone mapping and filtering are described separately. | Non-default settings, defaults, alias conflict, unknown mode and invalid numbers |
| Selection and geometry | `<xform>` supports `weight`, six `coefs`, six `post`, and a `chaos` row. Both affines use the order a b c d e f, giving (ax + cy + e, bx + dy + f). A missing trailing chaos multiplier is one. | Asymmetric off-diagonal coefficients, post-affine evaluation and transition row assertions |
| Variations | Registered names can have independent weights. `var1` selects a single registered FLAM3 ID. The parser evaluates named terms in canonical ID order. Unspecified variation weights are zero. A transform with no active terms maps to its post-affine translation. | Mixed terms, ID lookup, rejection of an unknown term or a mixture of named terms with `var1`; numerical vectors in the reference suite |
| Coloring | `color`, `color_speed`, legacy `symmetry`, and `opacity`. Legacy symmetry maps to (1 − symmetry) / 2. Supplying both forms of color speed is ambiguous and fails. | Non-default symmetry/opacity and model round trip |
| Final transform | One `<finalxform>` can carry affine, variation and coloring fields. A nonzero selection weight or any chaos row is rejected. | Final acceptance, repeated-final/weight/chaos errors, and input-order color defaults |
| Packed palette | One `<palette count="256" format="RGB">` containing six hexadecimal digits per entry. `RGBA` is supported when all alpha bytes are FF. Whitespace and either hex case are accepted. | RGB/RGBA at a 32 KiB stream boundary, truncated palette and transparency errors |
| Indexed stops | One or more `<color index="i" rgb="r g b"/>` children, or `rgba` with alpha equal to 255. Color components are decimal values in [0,255]. Missing indices interpolate linearly around the 256-entry ring. One stop fills the entire ring. | Interpolated interior, wrap-through-zero, one-stop and duplicate-index tests |
| Catalog references | `palette="-1"` is accepted with embedded color data. A catalog index or an absent embedded palette produces an error. | Indexed catalog and missing palette diagnostics |

For omitted settings, the reader follows the common FLAM3 defaults for the supported fields, including 100 × 100 pixels, scale 50, zoom and rotation zero, gamma and brightness 4, sample density 1, oversampling 1, vibrancy 1, a black background and step palette sampling. A regular transform has zero selection weight until specified. Transform color alternates with its input index, beginning at zero. Incoming transform colors mix at speed 0.5 by default. At least one regular transform must have positive weight. Default objects created directly by the C++ API have their own authoring defaults. [FLAM3 genome defaults](https://sources.debian.org/src/flam3/3.1.1%2Bds2-2/flam3.c/) · [FLAM3 transform initialization](https://sources.debian.org/src/flam3/3.1.1%2Bds2-2/variations.c/) · [FLAM3 XML reader](https://sources.debian.org/src/flam3/3.1.1%2Bds2-2/parser.c/)

## Save behavior

The serializer emits standalone UTF-8 XML for one selected flame. It writes both affine matrices, explicit supported settings and all 256 palette entries as decimal RGB stops. The decimal form preserves interpolated sub-byte channels to floating-point precision. A reopened file can therefore render a palette originally stored in packed hexadecimal. Whitespace, XML comments, the wrapper name, aliases, attribute order and the choice of packed versus indexed palette may change. Byte-level document preservation and integer-only palette export for older applications remain open. Older Apophysis versions must be tested with exported files before promising interoperability. [FLAM3 project documentation on integer palette export](https://github.com/scottdraves/flam3)

## Coverage of historical files

The parser stops at unknown attributes and unsupported nested elements, including filter radii/shapes, density-estimator settings, indexed palette catalogs, transparent palette entries, motion records, producer-specific execution orders, 3D coordinates, plugin variables and edit history. The error includes the first offending field or element where available and an XML location. Loading cannot silently erase those settings. Broader compatibility work needs source-version-specific defaults, a way to retain unfamiliar fields for editing/export, reference-render comparisons and fixtures from each target application.

| Target | Evidence in this checkpoint | Outstanding |
| --- | --- | --- |
| FLAM3 | Automated XML tests, a fixture derived from upstream geometry, source-derived conventions and offline numeric vectors. See `REFERENCES.md` for the reference test method. | Additional variation IDs, parameterized variations, alternate element encodings, filters, catalog and alpha palettes, and upstream application round trips |
| Original Apophysis 2.x | The FLAM3-style subset can represent shared 2D fields. | Archive exact release, test authentic untouched flames, import/export with that binary and compare its reference renders |
| Apophysis 7X | Producer version can be retained. | Audit versioned defaults, 3D/direct color, plugin attributes, transform-order controls and exports against actual 7X fixtures |
| Apophysis AV | A published AV Phoenix example exercised strict rejection. An explicitly reduced copy exercised supported geometry and its original packed palette as described below. | Untouched file import, AV-specific geometry, execution order, curve settings and render fidelity |

### AV corpus spot check

The August 9, 2022 article by samj Créations includes a complete four-transform *Demo* with its 256-entry palette. We extracted the displayed XML into a temporary file, normalizing nonbreaking spaces used for HTML indentation. The unmodified flame settings produced the expected diagnostic for the unsupported `angle` attribute. [Published AV parameters and reference image](https://samjcreations.blogspot.com/2022/?m=0)

For an isolated format check, a second temporary copy retained the one sinusoidal transform and the entire packed palette. It omitted the three transforms using the `flatten` plugin and removed `sinusoidal_use3D="0"` from the retained transform. The excluded flame-level fields were `angle`, `filter`, `gamma_threshold`, `estimator_radius`, `estimator_minimum`, `estimator_curve`, `enable_de`, `plugins`, `new_linear` and `curves`. The test loaded that copy, serialized it and reopened it with the same flame name, producer, transform count and palette size. Those reductions change the artwork. The original AV image and XML are external and the package does not redistribute them.
