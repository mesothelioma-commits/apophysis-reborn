# Native variation plugins

Apophysis Reborn 0.14.0 can load a custom 2D variation from a native library. Each plugin contributes one named operation to the CPU renderer. A transform can combine it with built-in variations, and a final transform can use it too. ABI v1 supports additive X/Y operations with real or integer parameters. Pre/post stages, direct color, Z coordinates and GPU kernels fall outside this interface. For automating edits or renders, see [Scripting](SCRIPTING.md).

## Try the bundled wave on Windows

1. Open **Tools → Plugins…** and select **sample_wave**. If the list is empty, choose **Add folder…** and select the `plugins` folder beside `apophysis-reborn.exe`.
2. Choose **Enable…**, review the module path and fingerprint, then choose **Trust and enable** for the bundled example. The library runs with your Windows permissions.
3. Close the manager. Use **Open project or library…** to open `examples/plugins/reborn-wave/sample-wave.flame` from the installed or extracted app folder. The sample uses the wave on its first two transforms.
4. Select **Transform 1**, then **Variations → Plugin variations…** in the right-hand panel. Change **amplitude** from `0.2` to `0.4` and choose **Apply** to see the change. For a file render select the CPU backend, or allow CPU fallback when using Automatic.

To render the same sample from the command line, open Git Bash in the folder containing `flame-cpu.exe` and run the commands below. Open `sample-wave.png` in the new folder under your home directory. The bundled module is already compiled.

```sh
mkdir -p "$HOME/Apophysis-Plugin-Test"
./flame-cpu.exe plugins info plugins/reborn-wave/reborn_wave.apoplugin
./flame-cpu.exe --plugin plugins/reborn-wave/reborn_wave.apoplugin \
  --flame examples/plugins/reborn-wave/sample-wave.flame \
  --samples 1000000 --output "$HOME/Apophysis-Plugin-Test/sample-wave.png"
```

## Using the plugin manager

Open **Tools → Plugins…**. Add a search folder or install a `.apoplugin` sidecar. Discovery reads sidecars and hashes module files without loading or executing them. It examines each search folder and one package-directory level beneath it. Invalid entries remain visible with a diagnostic.

Select an entry to inspect its stable ID, variation name, version, description, module path, SHA-256 fingerprint and ordered parameter definitions. **Install** copies only the sidecar and its referenced library into a new package directory under the user application's `plugins` directory; it refuses to overwrite an existing installation. Installation does not enable code. **Enable** shows the concrete library/fingerprint and requires a native-code trust decision. **Disable** prevents new parsing and new term construction using that variation. **Remove files** identifies both files before deletion and refuses enabled or retained modules.

Approved manifests and their fingerprints are stored in the application's settings. Startup loads those approvals before restoring documents or the render queue. A changed or missing binary produces a diagnostic and is not automatically approved. Failed approvals are retained so a repaired installation can be loaded later. Opening a flame or project never enables or installs a referenced native module. Removing a search folder does not revoke a separate saved enable approval; use **Disable** to do that.

The CLI accepts a leading `--plugin MANIFEST` for each library to enable during one invocation. Desktop approvals are stored separately. Use `plugins list FOLDER` to discover packages and `plugins info MANIFEST` to inspect one. Use `plugins install MANIFEST FOLDER` to copy a package or `plugins remove MANIFEST` to remove an unretained package.

## Trust and failure handling

**A native plugin runs inside the application with the user's permissions.** It can read/write files, execute other code, corrupt memory, crash the process or never return. The loader is not a sandbox, and a SHA-256 value is an identity check rather than a signature, publisher verification or security assessment. Native static initializers run when a module is explicitly loaded. Dependency libraries may also execute then.

The host validates manifest syntax and limits, relative package paths, platform/architecture, ABI version, descriptor size, parameter counts/types/defaults/ranges, duplicate names and exact descriptor-to-sidecar agreement. It rechecks the module fingerprint after loading. A callback's failure status, bounded error text, incompatible output size or nonfinite coordinates fail the render explicitly. C++ exceptions from a compatible native toolchain are caught at the call boundary as a diagnostic, but plugin authors must use return codes and must not throw across the C ABI.

Memory faults, malicious pointers, stack corruption, native hangs and process termination cannot be contained in this architecture. Cancellation is observed between host rendering work; it cannot interrupt a callback that does not return. File-output commits retain the renderer's existing atomic replacement behavior for reported errors, but a malicious module can bypass all host file APIs. An out-of-process protocol would be needed for meaningful native fault isolation and is not provided by ABI v1.

Use deterministic, reentrant callbacks with no mutable global render state. Every concurrent render may call the same module. The only supported random source is the supplied current-orbit callback; use of global `rand`, wall clocks, external data or other hidden state breaks deterministic rendering and checkpoint assumptions. The module fingerprint covers the selected DLL/shared object, not its transitive dependencies or the operating system's math implementation. Use self-contained modules where possible and keep dependencies stable for reproducible work.

## SDK and complete example

The public SDK is [src/plugin_api.h](../src/plugin_api.h). It uses fixed-width integer fields, C pointers and double-precision coordinates. No C++ objects, STL containers, allocation ownership or exceptions cross the ABI. The application and plugin must use the same platform/architecture and compatible native C ABI.

[examples/plugins/reborn-wave](../examples/plugins/reborn-wave) contains a complete C module, XML manifest template, standalone CMake project and flame. Its `sample_wave` operation is:

```text
x_out = x_in + amplitude × sin(frequency × y_in)
y_out = y_in + amplitude × sin(frequency × x_in)
```

Defaults are amplitude `0.2` and frequency `3`; ranges are `[-2,2]` and `[0,20]`. The host performs the term's weight multiplication once. Zero-weight plugin terms consume no random draws and do not invoke the callback.

### Build and distribute your own variation

1. Download the matching Reborn **source** archive or clone the repository. Start with a copy of `examples/plugins/reborn-wave`. Keep the SDK header from `src/plugin_api.h`. Building this standalone module requires CMake 3.20 or later and a C compiler. For the Windows commands below, install Visual Studio 2022 Build Tools with **Desktop development with C++** and use Git Bash at the source root.
2. In your copy, edit `evaluate` in `reborn_wave.c` to compute your unweighted X and Y result. Pick a unique ID such as `org.example.ripple` and a variation name such as `ripple`. Change the ID, name, version, description and any parameter definitions in **both** the C descriptor and `reborn_wave.apoplugin.in`. Keep parameter order, types and ranges identical.
3. Update the demo flame in your copy. Replace `sample_wave` wherever it occurs, including the weight, parameter attributes, saved ID/version attribute names and `var_order`. Set the saved ID and version values to match your descriptor. Keep the embedded palette so the result renders.
4. Build the module. The example below builds the unmodified template. For your own copy, change the `-S` path and use a separate build directory. The DLL and manifest must be in the same folder. The copy command places the Release DLL beside its generated manifest.

```sh
cmake -S examples/plugins/reborn-wave -B build/wave-plugin \
  -G "Visual Studio 17 2022" -A x64 -DAPO_PLUGIN_SDK="$PWD/src"
cmake --build build/wave-plugin --config Release
cp build/wave-plugin/Release/reborn_wave.dll build/wave-plugin/
```

5. In the editor, choose **Tools → Plugins… → Install…**, select the generated `build/wave-plugin/reborn_wave.apoplugin`, then enable your module and open your updated flame. To test using the CLI, use the same render command as above with the paths to your generated manifest and your flame.
6. Distribute the generated `.apoplugin` and matching x64 DLL together, plus a sample flame and any source or license files you wish or are required to include. The example can keep its filenames inside a separate package directory. If you rename the binary, update CMake's output name and manifest library filename together. A legacy Apophysis DLL requires a source port to this ABI.

On Linux, omit `-G "Visual Studio 17 2022" -A x64` and use a single-configuration build. CMake places `reborn_wave.so` next to the generated manifest. Both platforms use the same SDK header and flame format.

### Callback contract

Export exactly `apo_plugin_query(uint32_t requested_abi)` with `APO_PLUGIN_EXPORT` and `APO_PLUGIN_CALL`. Return a pointer to static `ApoPluginV1` metadata for ABI `1`, or null for an unsupported version. Metadata strings, parameter arrays and function pointers must remain valid until the module is unloaded. Struct sizes must equal the v1 SDK definitions; incompatible future layouts need a new ABI version.

`evaluate(input, parameters, count, output)` receives the already pre-affine transformed point, with any supported built-in pre-blur already applied. `input->affine` contains the transform's original `a,b,c,d,e,f` coefficients in XML order. `input->weight` is available as context for algorithms that need the weight internally, but the returned contribution itself must remain unweighted. The host sums each weighted result and then applies the post-affine transform.

The parameter array follows descriptor order and remains immutable during the call. Never retain pointers to inputs, parameters, outputs or random context. Write finite `output->x` and `output->y`, preserve `output->struct_size`, and return zero. On failure, return a nonzero status and optionally write a NUL-terminated explanation within `error[256]`; the host bounds the text. No plugin-owned allocation needs to be freed by the host.

Call `input->random_unit(input->random_context)` for each required random draw. It returns `[0,1)` from that orbit's current generator. A deterministic callback with a stable number/order of draws preserves the renderer's seed and thread-independent batch behavior. Changing draw order or formula semantics requires a plugin version change.

## Sidecar schema and limits

```xml
<apophysis-plugin abi="1" id="org.apophysis.reborn.wave"
  name="sample_wave" version="1.0.0"
  description="Ordinary 2D sine displacement" library="reborn_wave.dll">
  <parameter name="amplitude" default="0.2" min="-2" max="2" type="real"/>
  <parameter name="frequency" default="3" min="0" max="20" type="real"/>
</apophysis-plugin>
```

For Linux use the matching `.so` filename. The module path must be a single relative filename beside the manifest; absolute paths, parent traversal, path separators and module/manifest symlinks are rejected. A canonical path check prevents loading outside the package directory. Required root and parameter attributes are exact; unknown attributes/elements, text payloads and DTDs are errors.

| Field or resource | v1 constraint |
| --- | --- |
| Manifest | `.apoplugin`, regular file, at most 64 KiB |
| Main module | Regular nonsymlink file, 1 byte through 64 MiB; valid platform architecture at enable |
| Discovery | At most 1,024 manifest candidates per scan; no recursive arbitrary-depth traversal |
| ID | Unique among enabled modules; 1–128 characters; initial `a`–`z`, then lowercase ASCII letters, digits, `_`, `.` or `-` |
| Variation/parameter name | 1–64 characters; initial `a`–`z`, then lowercase ASCII letters, digits or `_` |
| Version / description | Nonempty, at most 64 / 2,048 UTF-8 bytes; no CR, LF or tab |
| Parameters | At most 64, unique names, stable descriptor order |
| Parameter type | `real` or `integer`; values transported as doubles |
| Range/default | Finite `min ≤ default ≤ max`; all integer bounds/defaults/values integral |
| Names | Built-ins, transform fields and reserved `pre_`, `post_`, `dc_`, `reborn_` prefixes rejected; enabled XML field collisions rejected |

The sidecar and exported metadata must agree exactly, including ordered parameter defaults/ranges. This prevents a harmless-looking sidecar from concealing a different queried descriptor, but does not make the native code itself trustworthy.

## Model, serialization and lifetime

`Term` retains its built-in enum value or a plugin sentinel plus `shared_ptr<const PluginVariation>` and immutable-per-snapshot parameter values. The shared descriptor owns the loaded module. Copying a Flame into an active preview or queued job pins that module independently of the global enabled registry. Disabling removes the registry entry without invalidating retained snapshots. Re-enabling an unchanged retained module reuses it. Replacing a module at the same path while old snapshots retain it is refused; release those snapshots or restart first.

Application helpers in [plugin.hpp](../src/plugin.hpp) provide `inspect_plugin`, `discover_plugins`, `enable_plugin`, `disable_plugin`, `enabled_plugins`, `install_plugin`, `remove_plugin`, `make_variation_term`, `term_name` and `term_parameters`. `PluginVariation::info()` exposes immutable metadata directly from a retained term; it does not require the variation to remain globally enabled. Its historical `enabled` flag reflects loading; use `inspect_plugin` or `enabled_plugins` for current registry status.

Flame XML uses named weights and prefixed parameters, for example `sample_wave="0.8"`, `sample_wave_amplitude="0.2"` and `sample_wave_frequency="3"`. Canonical serialization writes all parameter values, `reborn_plugin_id_sample_wave`, `reborn_plugin_version_sample_wave`, and a `var_order` list preserving mixed ordinary-term order. Missing parameters use declared defaults. Missing/disabled variations, unknown parameters, out-of-range values and mismatched saved IDs/versions produce errors. There is no substitution with linear or any other function, even for a custom weight of zero.

The identity/version references allow a source flame to use the same declared plugin implementation across platform builds. A module's SHA-256 is additionally included in render checkpoint identity, so a checkpoint rejects changed binary bytes even if the plugin author reused the same ID and version. Module parameters and order are already part of the canonical flame key. A rejected resume preserves the existing checkpoint. Persistent render queues also record the module identity separately from the portable flame XML and reject missing or changed modules before replacing the original queue state. A single flame cannot mix different native module identities under one variation name.

Native plugins render on the **CPU**. Automatic/Vulkan requests report that a native variation requires the CPU renderer and use the CPU only when the existing fallback option permits it. Disabling fallback turns that request into an explicit error. The Vulkan model packer also rejects plugin terms, so the sentinel cannot reach the compute shader as an arbitrary built-in index.

## Legacy Apophysis audit and migration

The original 2.09 loader and pinned 7X plugin SDK were inspected directly. The 2.09 loader resolves `PluginVarGetName`, variable enumeration/get/set/reset, `PluginVarCreate`/`Destroy`, `PluginVarInit`/`Init3D`, `PluginVarPrepare` and `PluginVarCalc` through `cdecl` exports. It stores host input/output pointers and a variation weight in per-instance native state. The 7X `Variation` structure additionally includes Z/color pointers, affine coefficients and a plugin-specific variable struct. Its parameter macros use integer offsets and cast pointers through `int`, which is an additional source-port concern for 64-bit code. [Original 2.09 source](https://downloads.sourceforge.net/project/apophysis/apophysis/Apophysis%202.09/Apophysis-2.09-src.7z) · [Pinned 7X SDK](https://github.com/xyrus02/apophysis-7x/blob/1cefa4099927e296d365412bd0b91b96f85606ca/src/Assets/Plugin/apoplugin.h)

The ABI and execution model differ from Reborn. A legacy DLL is not accepted merely because it matches the process architecture; it must export the Reborn query interface. PE32/x86 modules receive a specific migration diagnostic in an x64 host. Windows documents that a 64-bit process cannot load a 32-bit DLL, or vice versa; supporting one would need a separate helper process and a protocol rather than an in-process cast or wrapper. [Microsoft process interoperability](https://learn.microsoft.com/en-us/windows/win32/winprog64/process-interoperability)

| Legacy concept | Source conversion for supported ordinary 2D code |
| --- | --- |
| `FTx`, `FTy` | Read the callback's `input->x`, `input->y` |
| `FPx += VVAR * result`, `FPy += VVAR * result` | Assign unweighted results to `output->x/y`; Reborn accumulates the weight once |
| Affine `a`…`f` | Read `input->affine[0]`…`[5]` |
| Variable getter/setter/default macros | Declare ordered metadata in both sidecar and `ApoPluginParameterV1[]`; read `parameters[index]` |
| Successful legacy `Calc` returning `TRUE` | Return zero from v1 `evaluate`; nonzero means failure |
| `REAL` / `INTEGER` | Declare finite real/integer ranges and explicit defaults |
| Cyclic or nonzero-integer variables | Choose a representable valid range or implement explicit algorithm checks; v1 has no cyclic or excluded-zero metadata type |
| `Prepare` caches | Derive immutable local values from the parameter array; v1 exposes no mutable per-instance prepare state |
| Legacy global random calls | Replace with the supplied orbit RNG callback and preserve deliberate draw ordering |
| `Create` / `Destroy`, retained host pointers | Remove; use static immutable descriptor data and call-local calculation state |
| Pre/post stages, `TC`/direct color, Z coordinates, stateful orbit histories | Not faithfully expressible in ABI v1; report unsupported until corresponding host support exists |

[examples/plugins/legacy-port](../examples/plugins/legacy-port) supplies a complete compilable conversion example for a weighted gain/offset calculation, with explicit before/after operations. It is original teaching code, not a historical plugin copied under a different license. A real third-party source port must preserve its author and license obligations, compare numerical vectors across weights/parameter edges/singularities, and state any intentional behavior differences. No automatic conversion of arbitrary native DLL machine code is claimed.

Parameter names in the v1 descriptor are local suffixes: variation `example` plus parameter `variable` serializes as `example_variable`. For a legacy parameter already named `example_variable`, register the suffix `variable` to retain that XML spelling. Legacy attributes that do not follow the variation-prefix convention need an explicit source-file migration; the loader does not guess aliases or discard unknown parameters.

The historical 7X `example-plugin.c` writes direct color as well as coordinates, so reproducing it faithfully exceeds this v1 ABI. Simply discarding its color or Z writes would change the operation and is not an acceptable compatibility claim. [Pinned historical example](https://github.com/xyrus02/apophysis-7x/blob/1cefa4099927e296d365412bd0b91b96f85606ca/src/Assets/Plugin/example-plugin.c)

Inspected source identity:

| Source | SHA-256 |
| --- | --- |
| Original 2.09 archive | `c302b44194e3552999b5279c30900876e2063587e0137af02a3cad4caaec395a` |
| 2.09 `Source/varGenericPlugin.pas` | `cbf3850dce4bd65d0d2a522e6b180b277cc6a8810b5a3d1d12cfb2fc21efe559` |
| 7X `src/Assets/Plugin/apoplugin.h` | `1ac405bc3d6cc3a0895d85b2f7887edbd2583b769337f5c6478543f1e80a4042` |
| 7X `src/Assets/Plugin/example-plugin.c` | `118373278bb5030a53b62c69da1e1e0fd5ace71ab04de7be0bda3e79afaf5837` |

7X revision: `1cefa4099927e296d365412bd0b91b96f85606ca`. The audit did not vendor the legacy plugin SDK or DLLs into Reborn.

## Validation

`plugin-tests` builds and executes real fixture modules. It verifies discovery does not run a native constructor, a saved fingerprint is checked before loading, native ABI rejection, descriptor agreement, name/path/DTD/range guards, a standard SHA-256 vector, the explicit PE32 diagnostic, mathematical output/weight/affine context, current-orbit random draw counts and zero-weight behavior. It also checks parameter/XML/order round trips, source-preserving atomic edits, unavailable/disabled failures, live snapshot retention after disable, removal refusal while retained, GPU rejection/fallback and actual interrupted checkpoint identity changes.

The checkpoint test changes the module bytes while preserving its executable code, confirms that resume is refused without damaging the checkpoint, then restores the original binary and successfully resumes. Callback fixtures report a native error status or nonfinite output so these cannot silently disappear as skipped samples. `plugin-dialog-tests` verifies approved startup loads, refusal of missing/changed approval fingerprints, retention of failed approvals, registry/UI disable and settings persistence. The example and migration module also compile for Windows x64; Linux execution is not a substitute for testing their runtime behavior on a Windows machine.
