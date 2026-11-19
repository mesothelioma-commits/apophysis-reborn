# Apophysis Reborn 0.14.0

An AI assistant can now work with the flame open in the editor. Connect a desktop client that supports local stdio MCP servers, ask it to show the current preview, and try a Lua edit on a working copy. You choose which result to apply. Both Windows editions include the companion executable.

## Try the connection

1. Open a starter flame in Apophysis Reborn.
2. Choose **Tools → Local AI connection… → Copy configuration**. Add the entry to your MCP client, then choose **Enable for this session** in the editor.
3. Ask your assistant to show the selected flame and propose a copper-colored copy. In the editor, choose **Run**, inspect the preview, then choose **Apply result** to keep it or close the review to discard it. **Ctrl+Z** undoes an applied proposal.

Sharing ends when you disable it or close the editor. For fully local processing use a client and model running on your computer. Clients using hosted models may send shared flame data to their provider. [AI setup and troubleshooting](https://github.com/mesothelioma-commits/apophysis-reborn/blob/v0.14.0/docs/LOCAL_AI.md)

## Extend the editor

- **Plug-ins.** Enable the included wave and edit its amplitude. Use its C source, SDK header and manifest as a template for your own CPU variation. This release fixes the sample flame’s missing palette. [Install and build a plug-in](https://github.com/mesothelioma-commits/apophysis-reborn/blob/v0.14.0/docs/PLUGINS.md)
- **Scripts.** Write a Lua script to duplicate and recolor a flame, then run it in the editor or from PowerShell. [Write and run a script](https://github.com/mesothelioma-commits/apophysis-reborn/blob/v0.14.0/docs/SCRIPTING.md) · [Legacy script support](https://github.com/mesothelioma-commits/apophysis-reborn/blob/v0.14.0/docs/LEGACY_SCRIPTING.md)

## Downloads

For a per-user Windows installation, download the unsigned x64 Setup executable. For a portable installation, extract the entire portable ZIP before opening **apophysis-reborn.exe**. The source ZIP includes Git history. The offline guides ZIP includes the manual, examples, and plug-in SDK. Use **SHA256SUMS.txt** to verify a download. [Fork and build instructions](https://github.com/mesothelioma-commits/apophysis-reborn/blob/v0.14.0/docs/AI_FORK_GUIDE.md)

Apophysis Reborn is an unofficial clone of the original Apophysis fractal flame editor by Mark Townsend and contributors. AI generated the Reborn implementation and documentation. The repository owner directed and published the project. [Project credits and dependency notices](https://github.com/mesothelioma-commits/apophysis-reborn/blob/v0.14.0/docs/LICENSES.md)
