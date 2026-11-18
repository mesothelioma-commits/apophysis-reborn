# Windows freezes when closing Mutate or saving a seed change

The report described two sessions. In one, the editor stopped responding after a seed adjustment and Save. In the other, it stopped responding after the Mutate window closed. The screenshot showed a completed 2,000,000-sample Vulkan FP32 preview and “Preview paused.” The exact point where that Windows process stopped responding remains unknown.

## Changes in 0.10.2

**Closing creative windows.** The previous dialog destructor waited for candidate generation, detail rendering and thumbnail rendering. A driver or renderer taking a long time to finish could keep the main window from processing messages. Closing Mutate or Random now disconnects its receivers, cancels pending work and returns control to the editor. Rendering uses separate reusable workers for the main preview, dialog previews and thumbnails. Candidate generation checks for cancellation between candidates. Rapid reopening does not create additional workers for the same role.

**Windows graphics cleanup.** A thread-local owner in the previous version could destroy a Vulkan device and unload its library during thread detach. The new owner releases the device on the worker’s stack before thread exit. Successive preview passes and render-queue jobs can still share that worker’s device and pipeline. Application exit waits for work that is already running. A graphics driver that never returns can still delay process exit. See [Microsoft’s guidance on thread detach](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices).

**Saving.** Explicit project Save captures the current document, preview settings and image. A worker prepares the thumbnail, serializes the project, checks for external file changes and writes the result atomically. The editor shows a saving indicator and processes window messages. A successful save updates the title and clean indicator without reparsing the library or restarting the same preview. Edits made after the snapshot stay marked as unsaved.

## Regression checks

In an offscreen Qt test on Linux, both render calls were held for two seconds while the real dialog closed. The elapsed time includes delivery of the next queued main-thread event.

| Window | 0.10.1 | 0.10.2 |
| --- | ---: | ---: |
| Mutate | 2,003 ms | Under 1 ms |
| Random | 2,006 ms | Under 1 ms |

The 0.10.2 tests also close and reopen the explorer while both old renderers are blocked and complete a main preview before either renderer is released. An integration check uses the Ember Spiral fixture, a 2,000,000-sample preview, a native project and three different 64-bit seeds on each backend. It reopens the saved project, checks the exact seed, closes Mutate both immediately and during thumbnail rendering, and checks the resumed main image. The exercised backends were CPU and Mesa llvmpipe Vulkan with CPU fallback disabled. Physical Windows GPU testing remains open.

A separate Save test with a 2,110,536-byte flame library completed in 211 ms on the development machine. The editor serviced 143 timer callbacks during that save. The longest measured gap was 10 ms. The test checks the persisted seed, the saved bytes, edits made during the write and the clean indicator.

## Checking a Windows machine

Open the 0.10.2 application and confirm the version in **Help → System information**. To try the original sequence on a working copy, open a native project, change the random seed under **Edit → Preferences**, confirm the change and use **File → Save**. Reopen the project and check the seed. Open **Mutate** and close it, first immediately and then while thumbnails are appearing. With the main preview running before the dialog opens, the editor should accept input after it closes and show a fresh preview.

To compare the preview backends, run the following command in PowerShell from the extracted portable application folder. A subsequently opened project can restore its own saved preview backend. For that project choose CPU in Preferences.

```powershell
.\apophysis-reborn.exe --backend cpu
```

If a freeze recurs, the most useful details are the exact dialog and button, the release version and the last actual renderer/device shown in **Help → System information** before the freeze. If Save was interrupted, open the saved copy to check its contents.
