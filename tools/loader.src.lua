-- RobloxUtility loader stub
-- Decoded at runtime: fetches the obfuscated init.lua from raw GitHub
-- and executes it. This stub itself is fed through tools/obfuscate.py
-- so the resulting blob contains no plaintext URL.
local RAW = "https://raw.githubusercontent.com/ScriptMaster101/RobloxUtility/master/init.lua?v=5"
loadstring(game:HttpGet(RAW))()
