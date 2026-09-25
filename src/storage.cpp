#include "core.h"
#include <cstring>
#include <sstream>

namespace io {
std::wstring errorText(DWORD code) {
    wchar_t *buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    std::wstring result = buffer ? trim(buffer) : L"Windows error " + std::to_wstring(code);
    if (buffer)
        LocalFree(buffer);
    return result;
}
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr,
                                0, nullptr, nullptr);
    if (!n)
        throw std::runtime_error("Invalid Unicode");
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n)
        return L"Unknown error";
    std::wstring r(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n);
    return r;
}
std::wstring executablePath() {
    std::wstring p(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, p.data(), static_cast<DWORD>(p.size()));
    if (!n || n == p.size())
        throw std::runtime_error("Cannot locate InputOutput.exe");
    p.resize(n);
    return p;
}
std::wstring trim(std::wstring s) {
    auto a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos)
        return {};
    return s.substr(a, s.find_last_not_of(L" \t\r\n") - a + 1);
}
std::wstring newId() {
    GUID id{};
    check(CoCreateGuid(&id), L"Create identifier");
    wchar_t b[40]{};
    StringFromGUID2(id, b, 40);
    return b;
}
void check(HRESULT hr, const wchar_t *operation) {
    if (FAILED(hr))
        throw std::runtime_error(utf8(std::wstring(operation) + L": " + errorText(static_cast<DWORD>(hr))));
}
std::vector<uint8_t> readBytes(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("Could not read saved settings.");
    auto size = f.tellg();
    if (size < 0 || size > 4 * 1024 * 1024)
        throw std::runtime_error("Saved settings exceed the size limit.");
    std::vector<uint8_t> b(static_cast<size_t>(size));
    f.seekg(0);
    if (!b.empty() && !f.read(reinterpret_cast<char *>(b.data()), size))
        throw std::runtime_error("Saved settings are incomplete.");
    return b;
}
void writeAtomic(const std::filesystem::path &path, const std::vector<uint8_t> &bytes, bool backup) {
    std::filesystem::create_directories(path.parent_path());
    auto temp = path;
    temp += L".tmp";
    {
        Handle f(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                             nullptr));
        if (!f)
            throw std::runtime_error("Cannot write settings file.");
        DWORD written{};
        if (!WriteFile(f.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written != bytes.size() || !FlushFileBuffers(f.get()))
            throw std::runtime_error("Could not finish writing settings.");
    }
    if (backup && std::filesystem::exists(path)) {
        auto bak = path;
        bak += L".bak";
        if (!CopyFileW(path.c_str(), bak.c_str(), FALSE))
            throw std::runtime_error("Cannot back up previous settings.");
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace settings file.");
}
void log(const std::filesystem::path &folder, const std::wstring &message) {
    try {
        std::filesystem::create_directories(folder);
        auto p = folder / L"InputOutput.log";
        if (std::filesystem::exists(p) && std::filesystem::file_size(p) > 256 * 1024) {
            auto old = folder / L"InputOutput.previous.log";
            MoveFileExW(p.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        std::ofstream f(p, std::ios::app | std::ios::binary);
        SYSTEMTIME t{};
        GetLocalTime(&t);
        char stamp[48]{};
        snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u %02u:%02u:%02u ", t.wYear, t.wMonth, t.wDay, t.wHour,
                 t.wMinute, t.wSecond);
        f << stamp << utf8(message) << "\n";
    } catch (...) {
    }
}

namespace {
uint32_t checksum(const uint8_t *data, size_t size) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i)
        h = (h ^ data[i]) * 16777619u;
    return h;
}
struct Writer {
    std::vector<uint8_t> b;
    template <class T> void pod(const T &v) {
        static_assert(std::is_trivially_copyable_v<T>);
        auto p = reinterpret_cast<const uint8_t *>(&v);
        b.insert(b.end(), p, p + sizeof(T));
    }
    void str(const std::wstring &s) {
        auto u = utf8(s);
        if (u.size() > 16384)
            throw std::runtime_error("Setting text is too long.");
        pod(static_cast<uint32_t>(u.size()));
        b.insert(b.end(), u.begin(), u.end());
    }
    void flag(bool v) {
        pod(static_cast<uint8_t>(v));
    }
};
struct Reader {
    const std::vector<uint8_t> &b;
    size_t at{};
    template <class T> T pod() {
        if (at > b.size() || sizeof(T) > b.size() - at)
            throw std::runtime_error("Saved settings are truncated.");
        T v{};
        memcpy(&v, b.data() + at, sizeof(T));
        at += sizeof(T);
        return v;
    }
    uint32_t count(uint32_t max) {
        auto n = pod<uint32_t>();
        if (n > max)
            throw std::runtime_error("Saved settings contain an invalid count.");
        return n;
    }
    bool flag() {
        auto v = pod<uint8_t>();
        if (v > 1)
            throw std::runtime_error("Saved settings contain an invalid flag.");
        return v != 0;
    }
    std::wstring str() {
        auto n = count(16384);
        if (n > b.size() - at)
            throw std::runtime_error("Saved text is truncated.");
        std::string s(reinterpret_cast<const char *>(b.data() + at), n);
        at += n;
        auto w = wide(s);
        if (utf8(w) != s || w.find(L'\0') != std::wstring::npos)
            throw std::runtime_error("Saved text is invalid.");
        return w;
    }
};
void validateSnapshot(const Snapshot &s) {
    if (s.paths.empty() || s.paths.size() > 64 || s.modes.size() > 256 ||
        s.targets.size() != s.paths.size() || s.names.size() != s.paths.size())
        throw std::runtime_error("Invalid saved display layout.");
    std::set<std::wstring> targets;
    for (size_t i = 0; i < s.paths.size(); ++i) {
        auto &p = s.paths[i];
        if (s.targets[i].empty() || !targets.insert(s.targets[i]).second)
            throw std::runtime_error("Invalid monitor identity.");
        if (!(p.flags & DISPLAYCONFIG_PATH_ACTIVE) || p.sourceInfo.modeInfoIdx >= s.modes.size() ||
            p.targetInfo.modeInfoIdx >= s.modes.size())
            throw std::runtime_error("Invalid saved display mode index.");
        auto &sm = s.modes[p.sourceInfo.modeInfoIdx];
        auto &tm = s.modes[p.targetInfo.modeInfoIdx];
        if (sm.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE ||
            tm.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET || !sm.sourceMode.width ||
            !sm.sourceMode.height || sm.sourceMode.width > 65536 || sm.sourceMode.height > 65536 ||
            p.targetInfo.rotation < 1 || p.targetInfo.rotation > 4 || !p.targetInfo.refreshRate.Denominator)
            throw std::runtime_error("Invalid saved display mode.");
    }
}
void putSnapshot(Writer &w, const Snapshot &s) {
    validateSnapshot(s);
    w.pod(static_cast<uint32_t>(s.paths.size()));
    for (auto &p : s.paths)
        w.pod(p);
    w.pod(static_cast<uint32_t>(s.modes.size()));
    for (auto &m : s.modes)
        w.pod(m);
    for (auto &s : s.targets)
        w.str(s);
    for (auto &s : s.names)
        w.str(s);
}
Snapshot getSnapshot(Reader &r) {
    Snapshot s;
    auto n = r.count(64);
    for (uint32_t i = 0; i < n; ++i)
        s.paths.push_back(r.pod<DISPLAYCONFIG_PATH_INFO>());
    auto m = r.count(256);
    for (uint32_t i = 0; i < m; ++i)
        s.modes.push_back(r.pod<DISPLAYCONFIG_MODE_INFO>());
    for (uint32_t i = 0; i < n; ++i)
        s.targets.push_back(r.str());
    for (uint32_t i = 0; i < n; ++i)
        s.names.push_back(r.str());
    validateSnapshot(s);
    return s;
}
void putChoices(Writer &w, const std::vector<AudioChoice> &choices) {
    if (choices.size() > 256)
        throw std::runtime_error("Too many audio choices.");
    w.pod(static_cast<uint32_t>(choices.size()));
    for (auto &c : choices) {
        w.str(c.id);
        w.str(c.name);
        w.flag(c.shown);
    }
}
std::vector<AudioChoice> getChoices(Reader &r) {
    std::vector<AudioChoice> choices;
    std::set<std::wstring> ids;
    auto n = r.count(256);
    for (uint32_t i = 0; i < n; ++i) {
        AudioChoice c;
        c.id = r.str();
        c.name = r.str();
        c.shown = r.flag();
        if (c.id.empty() || c.name.empty() || !ids.insert(c.id).second)
            throw std::runtime_error("Invalid audio choices.");
        choices.push_back(c);
    }
    return choices;
}
} // namespace
std::vector<uint8_t> encodeConfig(const Config &c) {
    Writer w;
    w.pod(uint64_t{0x00314746434F4949});
    w.pod(uint32_t{1});
    w.pod(static_cast<uint32_t>(sizeof(DISPLAYCONFIG_PATH_INFO)));
    w.pod(static_cast<uint32_t>(sizeof(DISPLAYCONFIG_MODE_INFO)));
    w.flag(c.startup);
    w.flag(c.calls);
    w.str(c.outputFallback);
    w.str(c.inputFallback);
    if (c.displays.size() > 64)
        throw std::runtime_error("Too many display presets.");
    w.pod(static_cast<uint32_t>(c.displays.size()));
    for (auto &p : c.displays) {
        w.str(p.id);
        w.str(p.name);
        w.flag(p.shown);
        w.flag(p.legacyConfirmed);
        putSnapshot(w, p.snapshot);
    }
    putChoices(w, c.outputs);
    putChoices(w, c.inputs);
    w.pod(checksum(w.b.data(), w.b.size()));
    return w.b;
}
Config decodeConfig(const std::vector<uint8_t> &bytes) {
    if (bytes.size() < 24)
        throw std::runtime_error("Saved settings are incomplete.");
    uint32_t hash{};
    memcpy(&hash, bytes.data() + bytes.size() - 4, 4);
    if (hash != checksum(bytes.data(), bytes.size() - 4))
        throw std::runtime_error("Saved settings failed the integrity check.");
    Reader r{bytes};
    if (r.pod<uint64_t>() != 0x00314746434F4949 || r.pod<uint32_t>() != 1 ||
        r.pod<uint32_t>() != sizeof(DISPLAYCONFIG_PATH_INFO) ||
        r.pod<uint32_t>() != sizeof(DISPLAYCONFIG_MODE_INFO))
        throw std::runtime_error("Unsupported settings version.");
    Config c;
    c.startup = r.flag();
    c.calls = r.flag();
    c.outputFallback = r.str();
    c.inputFallback = r.str();
    auto n = r.count(64);
    std::set<std::wstring> ids;
    for (uint32_t i = 0; i < n; ++i) {
        DisplayPreset p;
        p.id = r.str();
        p.name = r.str();
        p.shown = r.flag();
        p.legacyConfirmed = r.flag();
        p.snapshot = getSnapshot(r);
        if (p.id.empty() || p.name.empty() || !ids.insert(p.id).second)
            throw std::runtime_error("Invalid display preset.");
        c.displays.push_back(p);
    }
    c.outputs = getChoices(r);
    c.inputs = getChoices(r);
    if (r.at != bytes.size() - 4)
        throw std::runtime_error("Unexpected saved data.");
    return c;
}
void saveConfig(const std::filesystem::path &folder, const Config &c) {
    writeAtomic(folder / L"settings.dat", encodeConfig(c), true);
}
Config loadConfig(const std::filesystem::path &folder) {
    auto path = folder / L"settings.dat";
    return std::filesystem::exists(path) ? decodeConfig(readBytes(path)) : Config{};
}
void saveSnapshot(const std::filesystem::path &path, const Snapshot &s) {
    Config c;
    c.displays.push_back({L"recovery", L"Previous setup", true, false, s});
    writeAtomic(path, encodeConfig(c));
}
Snapshot loadSnapshot(const std::filesystem::path &path) {
    auto c = decodeConfig(readBytes(path));
    if (c.displays.size() != 1)
        throw std::runtime_error("Invalid recovery layout.");
    return c.displays[0].snapshot;
}
bool startupEnabled() {
    DWORD bytes = 0;
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        L"InputOutput", RRF_RT_REG_SZ, nullptr, nullptr, &bytes) == ERROR_SUCCESS;
}
void setStartup(bool enabled) {
    HKEY key{};
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                                  nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error(utf8(errorText(result)));
    if (enabled) {
        auto cmd = L"\"" + executablePath() + L"\" --background";
        result = RegSetValueExW(key, L"InputOutput", 0, REG_SZ, reinterpret_cast<const BYTE *>(cmd.c_str()),
                                static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, L"InputOutput");
        if (result == ERROR_FILE_NOT_FOUND)
            result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error(utf8(errorText(result)));
}
} // namespace io
