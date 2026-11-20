# 0.17.0 development build

## Animate a property

Open **Tools → Animation timeline… → Property keys**, choose a frame with the playhead, enter a value and choose **Set key at playhead**. Repeat at a second frame. Each key controls the outgoing segment with Hold, Linear, Smooth, or Smoother interpolation. Keys override the full-flame snapshot only for that property. The first and last values extend to the clip boundaries. Rotation uses the degrees entered, so 0 to 720 produces two turns.

Tracks are available for zoom, rotation, brightness, gamma and vibrancy. Click a row to recall a key, replace it at the same frame, or remove it. Apply the timeline and save as an .apoproject. Adding a property key uses the version 2 animation envelope. Existing version 1 clips retain their snapshot interpolation.

## Compose and export

Save each animated layer as its own project, with its own palette, camera, transforms and timeline. Choose **Tools → Compose image sequences…**, then **Add layer…** for each project. Alternatively, select the first of a series of six-digit, numbered PNGs, such as frame_000000.png. Upper layers should have transparent backgrounds where lower layers should show through. Drag to reorder, with the bottom layer first.

Use the same canvas dimensions and frame numbers for all layers. Up to 1,000 frames are supported. Projects render through the CPU renderer to temporary transparent 16-bit PNG layers, with the chosen sample budget per frame. PNG inputs retain their existing rendering. The composition window keeps the layer list for the current session. Edit a source project and export again to pick up its changes.

**Export PNG frames…** publishes a new folder of transparent composite frames. **Export MP4…** requires FFmpeg with the libx264 encoder on PATH. Enter the playback frame rate as a fraction, such as 24000/1001. MP4 uses black behind transparent regions and pads odd dimensions to even dimensions. The exporter stages output before publication. Temporary storage must accommodate the rendered projects and output frames together.

## Classic flame compatibility

The Variation explorer includes 2D splits with independent X and Y offsets and elliptic (FLAM3 variation 62). The Apophysis AV Phoenix Edition archive contains both in the Splits Elliptic base form. At its original 1920 × 1200 size, CPU and Mesa llvmpipe each completed 4 million samples with zero invalid orbit steps. With seed 123456, the mean absolute CPU-to-Vulkan difference was 30.33 RGB levels out of 255. The comparison measures agreement between renderers. Producer pixel fidelity and physical GPU output remain open.

[Historical corpus and attribution](LEGACY_CORPUS.md) · [FLAM3 reference implementation](https://github.com/scottdraves/flam3/blob/1cdfc77275821182ad3c057e629c75a240dd52d3/variations.c) · [Splits formula comparison](https://github.com/thargor6/JWildfire/blob/master/src/org/jwildfire/create/tina/variation/SplitsFunc.java)

## Verification and distribution

Native Windows x64 and Linux CI passed on implementation commit `078d143`, including the local AI editor integration check. In the development workspace, 54 of 55 tests passed. The local AI test could not open its current-user socket. The Linux composition test exported a five-frame H.264 stream and verified its frame count and 24000/1001 playback rate with ffprobe. Setup installs the new payload before removing older registered versions for the current Windows account. Close the editor, renderer and AI companion before upgrading. Physical GPU and interactive Windows desktop checks remain open. [Native CI run](https://github.com/mesothelioma-commits/apophysis-reborn/actions/runs/44696133354)
