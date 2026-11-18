# Scripting examples

These examples were authored for Apophysis Reborn. Open them in **Tools → Script editor** or run them with the `flame-cpu script` command.

| File | Language | Result |
| --- | --- | --- |
| `palette-and-geometry.lua` | Lua | Edit current geometry, color coordinates, name and palette. |
| `seeded-generation.lua` | Lua | Append three reproducible generated flames and mutate their colors/geometry. |
| `batch-renders.lua` | Lua | Capture one PNG render request per library entry. Enable render requests and select an output folder. |
| `legacy-triangles.asc` | Apophysis Pascal subset | Build three affine transforms using audited historical calls and indices. |

Scripts edit a private document. Run reviews a result; Apply result creates one undo step. Stop or any error discards all pending edits and requests. The examples do not directly read or write arbitrary files.

[Lua API and limits](../../docs/SCRIPTING.md) · [Legacy subset and original-code verification](../../docs/LEGACY_SCRIPTING.md)
