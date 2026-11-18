# Embedded Lua 5.5.1

This directory vendors the unmodified `src` directory of the official Lua 5.5.1 release. `UPSTREAM_README` is the release's original README. `LICENSE` is the complete MIT license text carried at the end of upstream `src/lua.h`; the original notices also remain in the sources.

- Source archive: https://www.lua.org/ftp/lua-5.5.1.tar.gz
- Archive SHA-256: `1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce`
- Per-file source hashes: [provenance.json](provenance.json)
- Official manual: https://www.lua.org/manual/5.5/manual.html
- License: https://www.lua.org/license.html
- Official release list: https://www.lua.org/ftp/

M8 pins the maintained Lua 5.5 release line instead of taking a dependency on a system Lua installation. The exact patch release and checksums make native/Windows builds reproducible. There is no source patch to the vendored runtime.

## Build integration

CMake builds a static `apo-lua` library. The selected upstream `.c` files are compiled as **C++**; upstream `ldo.c` then implements protected Lua errors with C++ exceptions. This lets errors and allocation failures correctly unwind C++ strings, vectors and other objects inside Reborn host callbacks. Do not define `LUA_USE_LONGJMP` for this integration. Reborn includes `lua.h`, `lauxlib.h` and `lualib.h` directly with C++ linkage, matching this build; do not add an `extern "C"` wrapper or switch to `lua.hpp` without changing both sides consistently.

Linked translation units:

```text
lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem
lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio
lauxlib lbaselib lmathlib lstrlib ltablib lutf8lib
```

These are explicit filenames, not a wildcard. `lua.c`, `luac.c`, `linit.c`, `loadlib.c`, `liolib.c`, `loslib.c`, `ldblib.c`, and `lcorolib.c` are retained for provenance but are **not linked**. No standalone Lua interpreter/compiler, OS/I/O/package/debug/coroutine library is built into the application. Internal VM debug support is linked for the resource-limit hook; it is not the public Lua `debug` library.

The evaluator opens only selected libraries, replaces random and print with bounded host callbacks, removes unsupported base/string functions, and accepts text-only source. The runtime C API itself is not exposed to scripts. The source package includes the complete source needed to rebuild this embedded library; end users need no external Lua DLL or installation.

See [SCRIPTING.md](../../docs/SCRIPTING.md) for public API, capabilities, budgets, cancellation and scope. A Lua sandbox is separate from the host's explicit native-plugin loading boundary.
