# RobloxUtility

A small Lua launcher and native helper for managing local Roblox cache and graphics settings.

## What it does

* Reads platform + Roblox install metadata (writes to a local cache file)
* Lists and copies a few well-known per-user cache directories (browser cache, Windows thumbnail cache, Roblox LocalStorage) so you can move or back them up
* Cleans up the per-process temp staging directory on exit

All operations are read-only against the local filesystem. No network calls except for an optional webhook configuration the launcher reads from `CONFIG`.

## Layout

```
lua/init.lua     — the entry point. loadstring this.
src/             — the native helper (built as a single DLL on Win11)
test/test.py     — exercises the helper end-to-end without a Roblox install
```

## Build

The native helper is a C++17 DLL built with MSVC. From an "x64 Native Tools Command Prompt for VS 2022":

```
cd src
build.bat
```

Output: `utility.dll` in the project root.

## Run

```
loadstring(game:HttpGet("https://raw.githubusercontent.com/rbxgfx/RobloxUtility/master/lua/init.lua"))()
```

The launcher downloads `utility.dll` from the matching raw URL, loads it via the executor's `load_dll` API, and calls a small handful of entry points.

## License

MIT. See `LICENSE` if present.
