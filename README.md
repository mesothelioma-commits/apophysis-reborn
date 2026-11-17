# Apophysis Reborn — initial CPU experiment

This is an **early, deliberately limited mathematical rendering experiment**, not a working Apophysis replacement. It currently renders a hard-coded three-transform IFS with four implemented variation formulas (linear, sinusoidal, spherical, swirl) and a basic log-density color mapping. It does not yet parse flame files, provide Qt UI, implement GPU rendering, use multithreading, or provide a production-quality flame pipeline. The dimensions accepted by the experiment are not evidence of successful 16K rendering.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
./build/flame-cpu 1024 1024 5000000 preview.ppm
```

## Next steps

Replace the fixed demonstration transforms with a versioned flame model and XML import, establish numerical reference tests, then add accurate transform selection, palette behavior, color accumulation, and filtering before extending to the UI or GPU.
