# Apophysis Reborn 0.16.0

## Editable PNGs

Enable **Include editable flame in PNG** in the Render image window. Open the original exported PNG to recover an editable flame, its final camera and tone, the seed, and the sample count. Save the new project to keep editing. Anyone who receives that PNG can recover its flame data.

## Final-resolution render proofs

In **Render image…**, set the intended final size and sampling options, then choose **Proof a crop…**. Position a region of up to 1024 × 1024 output pixels and click **Render crop**. Inspect fine detail and alpha edges at actual pixel size. The proof uses the same camera, seed, backend selection and reconstruction filters as the final image. An allowed backend fallback is identified in the dialog.

Try different brightness, gamma and vibrancy values without repeating the sampling pass. **Use tone for render** passes the selected values to the pending queue job. The source flame keeps its original values. The final export performs its normal full-image render.

## Try this build

Open **Ember spiral**, render a centered proof, change gamma, and enable editable PNG export. Reopen the PNG in another session and check the gamma, seed and render dimensions. Also try closing the proof while sampling, switching flames, and saving.

[Step-by-step guide](USER_MANUAL.md#proof-and-editable-png) · [Image formats](IMAGE_FORMATS.md) · [Roadmap](ROADMAP.md) · [Plug-ins](PLUGINS.md) · [Scripts](SCRIPTING.md) · [Local AI](LOCAL_AI.md)
