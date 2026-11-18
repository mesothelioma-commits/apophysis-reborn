# Porting an ordinary legacy calculation

This directory contains an original teaching example, `migrated_gain.c`. It demonstrates the source conversion for an ordinary additive 2D variation whose old calculation would be:

```c
FPx += VVAR * (gain * FTx + offset);
FPy += VVAR * (gain * FTy);
```

The Reborn callback assigns the **unweighted** contribution:

```c
output->x = values[0] * input->x + values[1];
output->y = values[0] * input->y;
```

Reborn adds each weighted contribution and then applies the post-affine transform. Keeping the old `VVAR` multiplication would apply the weight twice. Keeping old `FPx`/`FPy` pointers or `+=` would incorrectly retain or access accumulation state. The exported descriptor and sidecar register `gain` and `offset` with the same ordered defaults and ranges. Missing XML parameter attributes receive these defaults; unknown or out-of-range values are errors.

Build from the repository root:

```sh
cmake -S examples/plugins/legacy-port -B build/migrated-gain
cmake --build build/migrated-gain --config Release
```

The output pair is `migrated_gain.apoplugin` plus `migrated_gain.so` on Linux or `migrated_gain.dll` on Windows. With a multi-configuration generator, keep the matching library beside the manifest when installing. Choose the same architecture as the application. An existing project can point `APO_PLUGIN_SDK` at the directory containing `plugin_api.h`.

Use `name="migrated_gain"`, `id="org.apophysis.reborn.migration.gain"` and `version="1.0.0"` only for this teaching example. A real port should choose its own stable identity, preserve the original algorithm's singularities and parameter behavior deliberately, and increment its version when behavior changes. Preserve the original author and license notices when adapting third-party source.

This example is not a binary adapter, an automatic converter, or a reproduction of the historical 7X `example-plugin.c`. That historical sample writes direct color and cannot be represented faithfully by Reborn ABI v1's ordinary 2D output. Pre/post stages, direct color, Z coordinates and stateful per-orbit behavior require additional host support; see [the legacy audit](../../../docs/PLUGINS.md#legacy-apophysis-audit-and-migration).
