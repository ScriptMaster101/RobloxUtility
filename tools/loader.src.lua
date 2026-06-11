-- RobloxUtility loader stub
-- Decoded at runtime: fetches the obfuscated init.lua from raw GitHub
-- and executes it. This stub itself is fed through tools/obfuscate.py
-- so the resulting blob contains no plaintext URL.
local RAW = "https://raw.githubusercontent.com/ScriptMaster101/RobloxUtility/main/init.lua"
loadstring(game:HttpGet(RAW))()
