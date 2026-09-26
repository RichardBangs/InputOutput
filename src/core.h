#pragma once
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace io {
template <class T> class ComPtr {
    T *p_{};

  public:
    ComPtr() = default;
    ~ComPtr() {
        if (p_)
            p_->Release();
    }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    T *operator->() const {
        return p_;
    }
    T *get() const {
        return p_;
    }
    T **put() {
        if (p_)
            p_->Release();
        p_ = nullptr;
        return &p_;
    }
};
class Handle {
    HANDLE h_{};

  public:
    explicit Handle(HANDLE h = nullptr) : h_(h) {}
    ~Handle() {
        if (h_ && h_ != INVALID_HANDLE_VALUE)
            CloseHandle(h_);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    HANDLE get() const {
        return h_;
    }
    explicit operator bool() const {
        return h_ && h_ != INVALID_HANDLE_VALUE;
    }
};
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComScope() {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};
std::wstring errorText(DWORD code);
std::string utf8(const std::wstring &text);
std::wstring wide(const std::string &text);
std::wstring executablePath();
std::wstring trim(std::wstring value);
std::wstring newId();
void check(HRESULT hr, const wchar_t *operation);
void writeAtomic(const std::filesystem::path &path, const std::vector<uint8_t> &bytes, bool backup = false);
std::vector<uint8_t> readBytes(const std::filesystem::path &path);
void log(const std::filesystem::path &folder, const std::wstring &message);

struct Snapshot {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    std::vector<std::wstring> targets;
    std::vector<std::wstring> names;
};
struct DisplayPreset {
    std::wstring id, name;
    // Retained for settings-file compatibility; saved presets no longer require confirmation.
    bool shown = true, legacyConfirmed = false;
    Snapshot snapshot;
};
struct AudioChoice {
    std::wstring id, name;
    bool shown = true;
};
struct Config {
    bool startup = true, calls = true;
    std::wstring outputFallback, inputFallback;
    std::vector<DisplayPreset> displays;
    std::vector<AudioChoice> outputs, inputs;
};
std::vector<uint8_t> encodeConfig(const Config &config);
Config decodeConfig(const std::vector<uint8_t> &bytes);
void saveConfig(const std::filesystem::path &folder, const Config &config);
Config loadConfig(const std::filesystem::path &folder);
void saveSnapshot(const std::filesystem::path &path, const Snapshot &snapshot);
Snapshot loadSnapshot(const std::filesystem::path &path);

Snapshot queryDisplays(UINT32 flags = QDC_ONLY_ACTIVE_PATHS);
Snapshot remapDisplays(const Snapshot &saved, const Snapshot &available);
bool sameDisplays(const Snapshot &a, const Snapshot &b);
std::wstring describeDisplays(const Snapshot &snapshot);
void applyDisplays(const Snapshot &saved);
void applyAndKeepDisplay(const std::function<void()> &apply, HANDLE keep, HANDLE revert);
bool displaysAvailable(const Snapshot &saved, const Snapshot &available);

struct AudioDevice {
    std::wstring id, name;
    EDataFlow flow = eRender;
    DWORD state{};
    // Hardware properties, separate from the friendly name or the user's menu label.
    std::wstring description, containerId, controller;
    bool hdmi = false;
};
std::vector<AudioDevice> audioDevices(EDataFlow flow);
std::map<std::wstring, std::wstring> reconnectAudioChoices(Config &config,
                                                           const std::vector<AudioDevice> &devices);
std::wstring defaultAudio(EDataFlow flow, ERole role = eMultimedia);
void setDefaultAudio(const std::wstring &id, EDataFlow flow, bool calls);
bool audioAvailable(const std::vector<AudioDevice> &devices, const std::wstring &id);
std::wstring deviceState(DWORD state);
HRESULT audioPolicyAvailable();
bool startupEnabled();
void setStartup(bool enabled);
int runSelfTests(const std::filesystem::path &folder);
void diagnose(const std::filesystem::path &folder);
int watchdog(const std::filesystem::path &path, const std::wstring &eventName, DWORD parent);
bool recoveryRequired(HANDLE keep, HANDLE revert, HANDLE owner, DWORD applyingMs = 30000);
} // namespace io
