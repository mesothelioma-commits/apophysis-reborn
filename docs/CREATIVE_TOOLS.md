# Creative tools guide — 0.7.0

The desktop editor provides native gradient, adjustment, random-generation and mutation workflows. Each tool previews its own working copy. Applying a result changes the flame library through the document session, so the committed operation can be undone from the main window. Cancel leaves the loaded flame unchanged.

The primary library, transform, saving and recovery controls are described in [EDITOR.md](EDITOR.md). Platform verification and outstanding acceptance checks are recorded in [TEST_PLAN.md](TEST_PLAN.md).

## Preview renderer

The four tools inherit the renderer, preferred device, GPU working budget and fallback policy selected in **Edit → Preferences**. They retain smaller preview sample limits: up to 400,000 for a selected flame and 80,000 for candidate thumbnails. A smaller configured preview limit is respected. The selected preview reports its execution path, and thumbnail tooltips identify their backend and any CPU fallback reason.

Vulkan uses FP32 orbit calculations and the existing CPU reconstruction. The CPU reference uses double precision and its original random stream, so switching backends can change preview pixels. Random generation and mutation still produce the same model for matching generation options and seed; the renderer controls how that model is sampled for display. Read [GPU_RENDERING.md](GPU_RENDERING.md) for capability checks, software-device labeling, memory limits and strict Vulkan behavior.

## Gradient editor

The gradient editor works with the selected flame's 256 palette entries. Its preview uses the current geometry and tone settings, so palette changes can be judged on the actual flame.

### Edit an individual color

Click or drag across the palette strip, or enter a **Palette index** from 0 to 255. Change the selected entry using **Red**, **Green** and **Blue** values from 0 to 255, the **Hue**, **Saturation %** and **Value %** fields, or **Choose color…**. These controls update the same palette entry.

**Render interpolation** controls how the renderer samples the stored palette. **Step / indexed** uses an indexed color; **Linear between indices** blends neighboring entries. This setting is separate from the interpolation method used to build a new gradient from color stops.

### Build from color stops

The **Color stops** tab provides editable Index, Red, Green and Blue columns. Indices and channels must be integers from 0 to 255. A gradient needs between 2 and 256 stops, with a distinct index for every stop.

**Sample palette** fills the table from six positions in the current palette: 0, 51, 102, 153, 204 and 255. This produces an editable approximation of the current colors. **Add index** inserts the currently selected palette index and color, and **Remove** deletes the selected row. Edit or remove duplicate indices before building.

Choose **RGB interpolation** or **HSV shortest hue path**, then press **Build gradient**. RGB blends channel values; HSV follows the shorter route around the hue wheel. Entries outside the first and last stop use the nearest endpoint color. Editing the table alone does not change the flame: Build is the step that replaces its working palette. After other palette operations, use Sample palette again if the table should reflect the new colors.

### Presets and a personal palette library

Choose a palette and press **Use preset**. Built-in presets are Ember, Glacier, Orchid, Forest, Copper, Spectrum and Monochrome. Palettes saved locally appear under **My library** in the same selector.

**Import…** accepts GIMP `.gpl` palettes and supported flame libraries in `.flame`, `.xml` or `.temp` files. A GPL file supplies 2–256 RGB colors; smaller sets are interpolated across the 256 palette entries. The desktop GPL reader accepts files up to 2 MiB. When a flame library contains more than one renderable entry, a picker selects the flame whose palette should be imported. Importing a palette does not import its source flame's geometry or tone settings.

**Export GPL…** writes the current palette to a chosen file. **Save to library…** asks for a name and saves a GPL copy in the application's per-user `palettes` folder. Saved presets remain available in later sessions. GPL export and local preset storage use 8-bit RGB values; the flame's embedded palette can retain fractional channel values produced by interpolation and adjustments. Use a saved flame library when that additional precision should accompany the artwork.

### Palette operations

| Control | Effect |
| --- | --- |
| Rotate | Cyclically shifts the palette by the entered number of indices. A positive shift moves the original index 0 toward larger indices. |
| Reverse | Reverses the order of all 256 entries. |
| Smooth | Averages each entry with nearby colors, including neighbors across the end/start boundary. Repeated presses apply further smoothing. |
| Randomize | Builds a new palette from the entered seed. Repeating the same seed reproduces that palette. |
| Hue rotation | Rotates all palette hues by the entered angle. |
| Saturation / Value multipliers | Scale the palette's HSV saturation and value, with results constrained to the color range. |
| Red / Green / Blue curve gamma | Apply separate gamma-shaped color curves to the stored RGB channels. |
| Contrast | Adjusts palette contrast around the middle of each channel's range. |

The fields in **Color adjustments** take effect when **Apply color adjustment** is pressed. Each press applies them to the current working palette, so repeated presses accumulate the effect. Palette curve gamma changes the palette colors themselves. Flame gamma, gamma threshold and density brightness are separate reconstruction settings in the Adjustment editor.

**Undo palette edit** and **Redo** navigate the dialog's palette-color history. Render interpolation remains a separate dialog setting. **Reset to original** restores both the opening palette and its render-interpolation setting. **Apply gradient** commits the finished result to the library as one undoable operation; **Cancel** abandons the working copy.

## Adjustment editor

The Adjustment editor groups supported flame settings into **Camera**, **Tone / color** and **Rendering** tabs. Numeric fields display editable decimal values. Invalid input is explained in the dialog and must be corrected before applying the result. **Reset to original** restores the settings from when the dialog opened.

### Camera and output

**Image width** and **Image height** set the saved output size. Enable **Keep aspect ratio** to capture the current ratio and maintain it while changing either dimension. Choosing a size preset sets both dimensions and updates that ratio.

**Center X/Y** move the saved camera. **Scale** sets pixels per world unit at the declared output size. **Zoom** is logarithmic: increasing it by one doubles the scale. **Rotation °** changes the camera angle. The document layer retains the historical radians-based `angle` convention when that is the original field.

**Frame visible orbit** estimates a useful center and scale from a deterministic orbit sample, excluding extreme outliers. It includes the effect of the final transform without feeding that final transform back into the orbit. The estimate covers the central 98% of the sampled geometry; use the numeric controls to refine the composition or include more distant structures. Degenerate or insufficiently finite orbits produce a diagnostic instead of replacing the camera.

The size list includes 16,384 × 16,384 as a stored output setting. Interactive previews remain viewport-sized. Version 0.7.0 file rendering uses bounded sampling and reconstruction strips, with separate RAM/disk budgets and full 16K acceptance. Use the main Render image dialog and queue for final output. [Rendering guide](HIGH_RES_RENDERING.md) · [Measured acceptance](HIGH_RES_ACCEPTANCE.md)

### Tone and background

| Control | Supported setting |
| --- | --- |
| Gamma | Positive gamma used by the selected producer's image reconstruction. |
| Gamma threshold | Nonnegative threshold for the renderer's low-density gamma transition. |
| Brightness | Nonnegative density-brightness multiplier. |
| Vibrancy | A value from 0 to 1 blending independent-channel and color-preserving gamma behavior. |
| Background | Opaque RGB background chosen through the color picker. |

The producer recorded in a legacy flame continues to determine its supported rendering conventions. This includes historical background-channel quantization. Palette hue, saturation, value and channel curves are edited in the Gradient editor.

### Rendering settings

**Sample density** is the saved quality value used to derive the default export sample budget. **Oversample** multiplies the histogram grid used for file rendering. The live preview uses its own sample budget and oversampling of one to remain responsive, so it is a composition and color preview rather than a measurement of final export sampling quality.

**Gaussian filter** exposes the supported nonnegative filter radius. **Source default** removes the explicit `filter` and `filter_shape` override. Historical Apophysis sources then use their Gaussian default radius of 0.5; generic sources use box reconstruction. Source default therefore follows the producing format's conventions.

**Apply adjustments** commits the edited fields as one undoable library operation. **Cancel** leaves the original settings intact.

## Random flame generator

The generator opens with a batch of nine candidates and renders their thumbnail previews. Select a tile to refine that candidate in the larger preview. **Candidates** accepts 1–24, so a value of one generates a single flame.

| Control | Effect |
| --- | --- |
| Seed | An unsigned integer from 0 to 18446744073709551615 determining the candidate set. |
| Lock seed when regenerating | Keeps the entered seed for Regenerate. When unlocked, Regenerate advances the seed before making the next set. |
| Choose new seed | Fills the seed field with a new value; press Regenerate to use it. |
| Minimum / Maximum transforms | Bounds the number of ordinary random transforms, from 2 to 12. Minimum must not exceed maximum. |
| Minimum / Maximum contraction | Bounds the affine basis scales. Values must be positive, less than one, and ordered from minimum to maximum. |
| Translation extent | Controls how far the randomly placed transforms can be offset. Use a positive value. |
| Rotation order | Adds rotational symmetry, from order 1 through 12. Order 1 adds no rotation branches. |
| Reflection symmetry | Adds a reflected linear branch. |
| Random palettes | Assigns each new flame a generated palette. When disabled, candidates inherit the palette from the flame used to open the tool. |
| Allowed variations | Determines the ordinary variations the generator can choose. All and None change the checked list. |

Choose at least one ordinary variation; `pre_blur` alone cannot define an ordinary transform. Symmetry is represented by additional explicit linear transforms. Those branches are outside the ordinary-transform count and are added even if linear is not selected in the allowlist.

Generated flames use the opening flame's output dimensions. The generator estimates a camera from each candidate's finite orbit. If the selected constraints cannot produce useful finite geometry, the dialog reports that result; change the seed or variation/geometry choices and regenerate.

**Add selected** appends the selected flame to the current library. **Add batch** appends all candidates in the displayed set. Each generated name contains the seed and its candidate number, and the library's name field can rename it afterward. These actions close the generator and commit one undoable library change. Save the library to retain the new entries on disk. **Cancel** closes the generator without adding candidates.

### Reproducing a result

Matching options and a matching seed reproduce the generator's model in this implementation. A locked seed is useful when changing one constraint and comparing its effect. Retain the resulting flame definitions when preserving an exact creative checkpoint, because future generator revisions or platform math differences can affect results.

Random generation and mutation are new deterministic Reborn workflows. They do not claim to reproduce a historical Apophysis application's random-number sequence or its exact random-flame and mutation algorithms.

## Mutation explorer

The Mutation explorer starts from the selected flame as **Original parent** and generates a grid of candidates. It shares the candidate count, seed, seed lock, new-seed button, allowlist and Regenerate behavior described above.

**Strength** ranges from 0 to 1. Zero retains the parent's rendering parameters; higher values allow larger perturbations. **Trend** ranges from −1 to 1 and biases the sign of numeric perturbations. Zero gives balanced changes. Trend is a numeric bias rather than a guarantee that the final image will move, grow or brighten in a particular direction.

Enable only the groups that should change:

| Mutation group | Current behavior |
| --- | --- |
| Geometry | Perturbs the pre-affine geometry through rotation, scale, shear and translation. |
| Variation weights / choices | Perturbs existing weights and may add or enable a checked variation. The allowlist controls new choices; it does not remove inherited variations that are unchecked. |
| Transform color | Changes palette coordinates and color speeds. |
| Variation parameters | Adjusts positive ordinary-transform selection weights and active JuliaN power/distance parameters. Zero-weight branches remain zero. |
| Palette | Adjusts the palette's hue, saturation and value. |
| Include final transform | Allows the enabled transform mutation groups to affect an existing final transform as well. It does not create a new final transform. |

Mutations retain the number and structure of transforms, the producing format, camera, output size and tone settings. A large geometric change can move visible structure outside the retained camera. The thumbnail tooltip reports when no samples reach the view; select another candidate or frame the saved result in the Adjustment editor.

### Compare and follow a branch

Select a candidate tile to view it at a larger size. **Compare with parent** switches that same preview to the current parent; uncheck it to return to the candidate. This compares the candidate against the parent of the current generation, which may be a later branch rather than Original parent.

**Use candidate as parent** adds the selected candidate to the dialog's history and generates a new set from it. The **Mutation history** selector can return to Original parent or any previously followed branch. Choosing a history item regenerates candidates from that parent with the current options and seed. Exploring another branch does not erase earlier history choices within the dialog.

### Keep the result

| Action | Library change |
| --- | --- |
| Apply to current | Applies the selected candidate's supported creative changes to the current flame while retaining its name and unrelated original XML. |
| Add selected | Appends the candidate as a separate named flame. |
| Add batch | Appends every candidate currently displayed. |
| Add branch history | Appends the followed ancestors of the current branch, in order, and then the selected candidate. The original parent is already in the library and is not duplicated by this action. |
| Cancel | Leaves the loaded library unchanged. |

The history selector is temporary to the open explorer. **Add branch history** converts the chosen ancestry into actual library entries that can be saved, reopened and browsed independently. It saves the current branch's ancestry, not every alternate branch visited in the dialog. Save the library after accepting results to retain those entries on disk. Each accepted operation is one main-window undo step.

## Source preservation and saved work

Adjustments patch the requested attributes rather than rewriting the entire loaded XML document. Existing camera and sampling aliases are retained where applicable. Palette edits replace the selected entry's palette data and retain the document encoding, unrelated entries, historical edit records, comments and processing instructions. An indexed or named palette reference becomes an explicit embedded palette with `palette="-1"`.

Appending a flame to a standalone file adds a collection wrapper while retaining the original flame's bytes. Duplicating an existing entry preserves even unsupported source fields. Generated models are validated before insertion, and deleting entries retains at least one flame in the collection. The main window's undo history, modified marker, save-conflict checks and recovery copies also apply to these operations.

Palette libraries and exported GPL files are independent saved files. Canceling a Gradient dialog does not delete a preset or GPL file explicitly saved from that dialog.

## Current scope

The RGB channel curves in the Gradient editor are baked into the embedded palette. They do not implement the legacy `curves` reconstruction parameter. Non-default legacy reconstruction curves, active density estimation, additional filter families and direct-color extensions remain open. Version 0.7.0 final file exports support straight alpha in PNG/TIFF and true 16-bit channels; these are separate from the creative preview's opaque display. [Output semantics](IMAGE_FORMATS.md)

The tools can preview through the multithreaded CPU renderer or the Vulkan compute backend, with an explicit fallback policy. Physical GPU/platform acceptance, tiled high-resolution output and native 16K acceptance are recorded separately. See [GPU_RENDERING.md](GPU_RENDERING.md), [FEATURE_MATRIX.md](FEATURE_MATRIX.md) and [COMPATIBILITY.md](COMPATIBILITY.md) for the broader inventory.
