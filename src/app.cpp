#include "app.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <sstream>

namespace io {
namespace {
constexpr int TimerRefresh = 1, TimerSettle = 3, TimerSmoke = 4;
constexpr GUID TrayGuid = {0xf1d4b0a9, 0x2cd8, 0x4ca5, {0x97, 0x45, 0x45, 0x6b, 0x9c, 0x42, 0x44, 0x26}};
std::wstring exceptionText() {
    try {
        throw;
    } catch (const std::exception &e) {
        return wide(e.what());
    } catch (...) {
        return L"An unexpected error occurred.";
    }
}
void copyText(wchar_t *dest, size_t size, const std::wstring &text) {
    wcsncpy_s(dest, size, text.c_str(), _TRUNCATE);
}
std::wstring deviceLabel(const std::vector<AudioChoice> &choices, const std::wstring &id) {
    for (auto &c : choices)
        if (c.id == id)
            return c.name;
    return {};
}
std::wstring quote(const std::wstring &s) {
    return L"\"" + s + L"\"";
}
} // namespace
HRESULT DeviceNotifications::QueryInterface(REFIID id, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, __uuidof(IMMNotificationClient))) {
        *out = static_cast<IMMNotificationClient *>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}
App::~App() {
    if (worker_.joinable())
        worker_.join();
    closeGuardian();
    if (notifications_) {
        if (enumerator_.get())
            enumerator_->UnregisterEndpointNotificationCallback(notifications_);
        notifications_->Release();
    }
    removeTray();
    for (auto &[dpi, fonts] : fontCache_) {
        (void)dpi;
        for (auto font : fonts)
            DeleteObject(font);
    }
    if (icon_)
        DestroyIcon(icon_);
}
void App::createFonts(WindowUi &ui, UINT dpi) {
    ui.dpi = dpi ? dpi : 96;
    if (auto cached = fontCache_.find(ui.dpi); cached != fontCache_.end()) {
        ui.font = cached->second[0];
        ui.smallFont = cached->second[1];
        ui.titleFont = cached->second[2];
        ui.iconFont = cached->second[3];
        return;
    }
    ui.font =
        CreateFontW(-MulDiv(10, ui.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    ui.smallFont =
        CreateFontW(-MulDiv(9, ui.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    ui.titleFont =
        CreateFontW(-MulDiv(20, ui.dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    ui.iconFont = CreateFontW(-MulDiv(12, ui.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, L"Segoe Fluent Icons");
    // Keep fonts alive while a window on a different-DPI monitor still uses them.
    fontCache_[ui.dpi] = {ui.font, ui.smallFont, ui.titleFont, ui.iconFont};
}
void App::updateWindowDpi(HWND window, UINT dpi) {
    if (!window || !dpi)
        return;
    auto &ui = window == settings_ ? settingsUi_ : popupUi_;
    if (ui.dpi == dpi && ui.font)
        return;
    if (window == popup_)
        popupScroll_ = MulDiv(popupScroll_, dpi, ui.dpi);
    createFonts(ui, dpi);
}
void App::queueLayout(HWND window) {
    if (!window)
        return;
    auto &ui = window == settings_ ? settingsUi_ : popupUi_;
    if (!ui.layoutQueued)
        ui.layoutQueued = PostMessageW(window, WM_RELAYOUT, 0, 0) != FALSE;
}
HWND App::control(HWND parent, const wchar_t *cls, const std::wstring &text, DWORD style, int x, int y, int w,
                  int h, int id) {
    const auto &ui = parent == settings_ ? settingsUi_ : popupUi_;
    if (wcscmp(cls, L"BUTTON") == 0 && (style & BS_TYPEMASK) == BS_PUSHBUTTON)
        style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    if (wcscmp(cls, L"EDIT") == 0)
        style |= WS_BORDER;
    HWND c = CreateWindowExW(0, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, ui.scale(x), ui.scale(y),
                             ui.scale(w), ui.scale(h), parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    if (!c)
        throw std::runtime_error("Could not create a Windows control.");
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(ui.font), TRUE);
    if (wcscmp(cls, WC_LISTVIEWW) == 0)
        SetWindowTheme(c, L"Explorer", nullptr);
    if (wcscmp(cls, L"EDIT") == 0)
        SendMessageW(c, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(ui.scale(7), ui.scale(7)));
    return c;
}
void App::addTray() {
    tray_ = {};
    tray_.cbSize = sizeof(tray_);
    tray_.hWnd = owner_;
    tray_.uID = 1;
    tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_GUID | NIF_SHOWTIP;
    tray_.guidItem = TrayGuid;
    tray_.uCallbackMessage = WM_TRAY;
    tray_.hIcon = icon_;
    copyText(tray_.szTip, std::size(tray_.szTip), L"InputOutput");
    if (!Shell_NotifyIconW(NIM_ADD, &tray_)) {
        SetTimer(owner_, 5, 1500, nullptr);
        return;
    }
    tray_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &tray_);
}
void App::removeTray() {
    if (tray_.hWnd)
        Shell_NotifyIconW(NIM_DELETE, &tray_);
    tray_.hWnd = nullptr;
}
void App::notifyError(const std::wstring &text) {
    notice_ = text;
    log(folder_, text);
    if (settings_ && IsWindowVisible(settings_))
        MessageBoxW(settings_, text.c_str(), L"InputOutput", MB_OK | MB_ICONWARNING);
    else {
        tray_.uFlags = NIF_INFO | NIF_GUID;
        copyText(tray_.szInfoTitle, std::size(tray_.szInfoTitle), L"InputOutput");
        copyText(tray_.szInfo, std::size(tray_.szInfo), text);
        tray_.dwInfoFlags = NIIF_WARNING;
        Shell_NotifyIconW(NIM_MODIFY, &tray_);
    }
    if (popup_ && IsWindowVisible(popup_))
        buildPopup();
}
bool App::save(const Config &before) {
    try {
        if (readOnly_)
            throw std::runtime_error(
                "Settings are read-only because the existing file could not be loaded. Open the data folder "
                "and restore settings.dat.bak, or move settings.dat aside, then restart InputOutput.");
        saveConfig(folder_, config_);
        return true;
    } catch (...) {
        config_ = before;
        notifyError(exceptionText());
        return false;
    }
}
void App::refresh(bool display) {
    if (smoke_)
        return;
    outputs_ = audioDevices(eRender);
    inputs_ = audioDevices(eCapture);
    currentOutput_ = defaultAudio(eRender);
    currentInput_ = defaultAudio(eCapture);
    if (display) {
        current_ = queryDisplays();
        available_ = queryDisplays(QDC_ALL_PATHS);
    }
    if (!readOnly_) {
        auto updated = config_;
        auto replacements = reconnectAudioChoices(updated, outputs_);
        if (!replacements.empty()) {
            auto before = config_;
            config_ = std::move(updated);
            if (save(before)) {
                if (auto found = replacements.find(wantedOutput_); found != replacements.end())
                    wantedOutput_ = found->second;
                // Keep an open Settings selection and unsaved name edit attached to
                // the same saved choice when its Windows endpoint changes.
                if (page_ == 1)
                    for (auto &device : settingsDevices_)
                        if (auto found = replacements.find(device.id); found != replacements.end())
                            device.id = found->second;
                for (const auto &[oldId, newId] : replacements)
                    log(folder_, L"Reconnected HDMI audio menu entry: " + oldId + L" -> " + newId);
                refreshSettingsDevices();
            }
        }
    }
    std::wstring tip = L"InputOutput";
    auto o = deviceLabel(config_.outputs, currentOutput_), m = deviceLabel(config_.inputs, currentInput_);
    if (!o.empty())
        tip += L"\nAudio: " + o;
    if (!m.empty())
        tip += L"\nMicrophone: " + m;
    tray_.uFlags = NIF_TIP | NIF_GUID | NIF_SHOWTIP;
    copyText(tray_.szTip, std::size(tray_.szTip), tip);
    if (tray_.hWnd)
        Shell_NotifyIconW(NIM_MODIFY, &tray_);
}
void App::startJob(std::function<void()> action, std::function<void(bool)> completion) {
    if (busy_)
        return;
    if (worker_.joinable())
        worker_.join();
    busy_ = true;
    completion_ = std::move(completion);
    notice_.clear();
    if (settings_)
        EnableWindow(settings_, FALSE);
    buildPopup();
    worker_ = std::thread([this, action = std::move(action)] {
        ComScope com;
        auto result = std::make_unique<JobResult>();
        try {
            action();
        } catch (...) {
            result->error = exceptionText();
        }
        if (PostMessageW(owner_, WM_JOB, 0, reinterpret_cast<LPARAM>(result.get())))
            result.release();
    });
}
void App::switchAudio(const std::wstring &id, EDataFlow flow) {
    if (busy_)
        return;
    preserveUntil_ = 0;
    bool calls = config_.calls;
    startJob(
        [=, this] {
            if (!smoke_)
                setDefaultAudio(id, flow, calls);
        },
        [=, this](bool ok) {
            if (ok) {
                if (flow == eRender)
                    currentOutput_ = id;
                else
                    currentInput_ = id;
            }
            buildPopup();
        });
}
void App::beginGuardian(const Snapshot &previous) {
    if (guardian_ && WaitForSingleObject(guardian_, 0) == WAIT_TIMEOUT)
        throw std::runtime_error(
            "The previous display recovery is still running. Please wait before switching again.");
    closeGuardian();
    recovery_ = folder_ / L"display-recovery.dat";
    saveSnapshot(recovery_, previous);
    std::wstring event =
        L"Local\\InputOutput.Recovery." + std::to_wstring(GetCurrentProcessId()) + L"." + newId();
    keep_ = CreateEventW(nullptr, TRUE, FALSE, (event + L".keep").c_str());
    revert_ = CreateEventW(nullptr, TRUE, FALSE, (event + L".revert").c_str());
    ready_ = CreateEventW(nullptr, TRUE, FALSE, (event + L".ready").c_str());
    if (!keep_ || !revert_ || !ready_)
        throw std::runtime_error("Could not prepare display recovery.");
    std::wstring command = quote(executablePath()) + L" --watchdog " + quote(recovery_.wstring()) + L" " +
                           quote(event) + L" " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &si, &pi))
        throw std::runtime_error("Could not start display recovery.");
    CloseHandle(pi.hThread);
    guardian_ = pi.hProcess;
    if (WaitForSingleObject(ready_, 5000) != WAIT_OBJECT_0) {
        SetEvent(keep_);
        WaitForSingleObject(guardian_, 5000);
        throw std::runtime_error("Display recovery did not become ready; no display change was made.");
    }
}
void App::closeGuardian() {
    for (auto *h : {&keep_, &revert_, &ready_, &guardian_}) {
        if (*h)
            CloseHandle(*h);
        *h = nullptr;
    }
}
void App::switchDisplay(size_t index) {
    if (busy_ || index >= config_.displays.size())
        return;
    auto preset = config_.displays[index];
    if (sameDisplays(preset.snapshot, current_))
        return;
    wantedOutput_ = currentOutput_;
    wantedInput_ = currentInput_;
    startJob(
        [=, this] {
            if (!smoke_) {
                auto previous = queryDisplays();
                beginGuardian(previous);
                log(folder_, L"Applying display preset: " + preset.name);
                try {
                    // Commit every saved preset as soon as Windows verifies it, including older
                    // presets whose legacy confirmation flag is false.
                    applyAndKeepDisplay([&] { applyDisplays(preset.snapshot); }, keep_, revert_);
                    if (WaitForSingleObject(guardian_, 5000) != WAIT_OBJECT_0)
                        throw std::runtime_error(
                            "Display recovery did not finish cleanly. Please wait before switching again.");
                    DWORD exitCode{};
                    if (!GetExitCodeProcess(guardian_, &exitCode) || exitCode != 0)
                        throw std::runtime_error("The display switch did not finish before recovery ran. "
                                                 "Check the current screens and try again.");
                    log(folder_, L"Display preset kept: " + preset.name);
                } catch (...) {
                    if (revert_)
                        SetEvent(revert_);
                    if (guardian_)
                        WaitForSingleObject(guardian_, 15000);
                    throw;
                }
            }
        },
        [=, this](bool ok) {
            if (!guardian_ || WaitForSingleObject(guardian_, 0) == WAIT_OBJECT_0)
                closeGuardian();
            if (ok && smoke_)
                current_ = preset.snapshot;
            preserveUntil_ = GetTickCount64() + 6000;
            preserveAttempts_ = 0;
            SetTimer(owner_, TimerSettle, 700, nullptr);
            // A dismissed switcher stays dismissed when the background work completes.
            buildPopup();
            if (IsWindowVisible(popup_))
                positionPopup();
        });
}
void App::reconcileAudio() {
    if (smoke_ || busy_)
        return;
    auto oldOutput = currentOutput_, oldInput = currentInput_;
    refresh(false);
    auto targetOutput = currentOutput_, targetInput = currentInput_;
    bool preserving = GetTickCount64() < preserveUntil_ && preserveAttempts_ < 3;
    if (preserving) {
        if (audioAvailable(outputs_, wantedOutput_))
            targetOutput = wantedOutput_;
        else if (audioAvailable(outputs_, config_.outputFallback))
            targetOutput = config_.outputFallback;
        if (audioAvailable(inputs_, wantedInput_))
            targetInput = wantedInput_;
        else if (audioAvailable(inputs_, config_.inputFallback))
            targetInput = config_.inputFallback;
    } else {
        if (!oldOutput.empty() && !audioAvailable(outputs_, oldOutput) &&
            audioAvailable(outputs_, config_.outputFallback))
            targetOutput = config_.outputFallback;
        if (!oldInput.empty() && !audioAvailable(inputs_, oldInput) &&
            audioAvailable(inputs_, config_.inputFallback))
            targetInput = config_.inputFallback;
    }
    const bool changeOut = !targetOutput.empty() && targetOutput != currentOutput_,
               changeIn = !targetInput.empty() && targetInput != currentInput_;
    if (changeOut || changeIn) {
        ++preserveAttempts_;
        bool calls = config_.calls;
        startJob(
            [=] {
                if (changeOut)
                    setDefaultAudio(targetOutput, eRender, calls);
                if (changeIn)
                    setDefaultAudio(targetInput, eCapture, calls);
            },
            [this](bool) { buildPopup(); });
    } else if (popup_ && IsWindowVisible(popup_))
        buildPopup();
}
void App::recoverAtLaunch() {
    auto path = folder_ / L"display-recovery.dat";
    if (!std::filesystem::exists(path) || smoke_)
        return;
    if (MessageBoxW(nullptr, L"A display switch did not finish. Restore the previous screen setup?",
                    L"InputOutput recovery", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        applyDisplays(loadSnapshot(path));
        DeleteFileW(path.c_str());
    } else {
        auto kept = folder_ / L"display-recovery.previous.dat";
        MoveFileExW(path.c_str(), kept.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

int App::run(bool showSettingsFirst, bool showPopupFirst) {
    theme_.refresh();
    std::filesystem::create_directories(folder_);
    const bool firstRun = !std::filesystem::exists(folder_ / L"settings.dat");
    try {
        config_ = loadConfig(folder_);
    } catch (...) {
        notice_ = exceptionText();
        readOnly_ = true;
    }
    const UINT initialDpi = GetDpiForSystem();
    createFonts(popupUi_, initialDpi);
    createFonts(settingsUi_, initialDpi);
    icon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(101), IMAGE_ICON, popupUi_.scale(32),
                                          popupUi_.scale(32), LR_DEFAULTCOLOR));
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance_;
    wc.hIcon = icon_;
    wc.hIconSm = icon_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpfnWndProc = ownerProc;
    wc.lpszClassName = L"InputOutput.Owner";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = popupProc;
    wc.lpszClassName = L"InputOutput.Popup";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = settingsProc;
    wc.lpszClassName = L"InputOutput.Settings";
    RegisterClassExW(&wc);
    auto ownerTitle = L"InputOutput." + std::to_wstring(std::hash<std::wstring>{}(folder_.wstring()));
    owner_ = CreateWindowExW(0, L"InputOutput.Owner", ownerTitle.c_str(), WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                             nullptr, instance_, this);
    if (!owner_)
        throw std::runtime_error("Cannot create InputOutput.");
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    popup_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT, L"InputOutput.Popup",
                             L"InputOutput", WS_POPUP | WS_BORDER | WS_VSCROLL, 0, 0, popupUi_.scale(330),
                             popupUi_.scale(400), owner_, nullptr, instance_, this);
    updateWindowDpi(popup_, GetDpiForWindow(popup_));
    DWORD corner = 2;
    DwmSetWindowAttribute(popup_, 33, &corner, sizeof(corner));
    addTray();
    if (!smoke_) {
        recoverAtLaunch();
        // Subscribe before the initial scan so a TV arriving during startup is not missed.
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                       __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void **>(enumerator_.put())))) {
            notifications_ = new DeviceNotifications(owner_);
            if (FAILED(enumerator_->RegisterEndpointNotificationCallback(notifications_)))
                log(folder_, L"Audio notifications unavailable; devices will refresh when the menu opens.");
        }
        try {
            refresh();
        } catch (...) {
            notifyError(exceptionText());
        }
    }
    if (firstRun && !readOnly_) {
        saveConfig(folder_, config_);
        if (!noStartup_)
            try {
                setStartup(config_.startup);
            } catch (...) {
                notifyError(exceptionText());
            }
    }
    if (!notice_.empty())
        notifyError(notice_);
    if (showPopupFirst)
        showPopup();
    else if (showSettingsFirst || firstRun)
        showSettings();
    if (uiSmoke_)
        SetTimer(owner_, TimerSmoke, 700, nullptr);
    log(folder_, L"InputOutput started.");
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (settings_ && IsWindowVisible(settings_) && IsDialogMessageW(settings_, &msg))
            continue;
        if (popup_ && IsWindowVisible(popup_) && IsDialogMessageW(popup_, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    log(folder_, L"InputOutput stopped.");
    return static_cast<int>(msg.wParam);
}
LRESULT CALLBACK App::ownerProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    App *app = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        app->owner_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(hwnd, message, w, l);
    try {
        return app->ownerMessage(message, w, l);
    } catch (...) {
        app->notifyError(exceptionText());
        return 0;
    }
}
LRESULT App::ownerMessage(UINT message, WPARAM w, LPARAM l) {
    if (message == taskbarCreated_ && taskbarCreated_) {
        addTray();
        return 0;
    }
    switch (message) {
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
        theme_.refresh();
        queueLayout(settings_);
        queueLayout(popup_);
        return 0;
    case WM_APP + 4:
        showSettings();
        return 0;
    case WM_OPENPOPUP:
        showPopup();
        return 0;
    case WM_TRAY:
        if (LOWORD(l) == NIN_SELECT || LOWORD(l) == NIN_KEYSELECT || LOWORD(l) == WM_CONTEXTMENU) {
            showPopup();
        }
        return 0;
    case WM_DISPLAYCHANGE:
        queueLayout(popup_);
        queueLayout(settings_);
        SetTimer(owner_, TimerRefresh, 400, nullptr);
        return 0;
    case WM_DEVICES:
        SetTimer(owner_, TimerRefresh, 250, nullptr);
        return 0;
    case WM_POWERBROADCAST:
        if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND)
            SetTimer(owner_, TimerRefresh, 1000, nullptr);
        return TRUE;
    case WM_TIMER:
        if (w == TimerRefresh) {
            if (busy_)
                return 0;
            KillTimer(owner_, TimerRefresh);
            reconcileAudio();
            if (!busy_)
                try {
                    current_ = queryDisplays();
                    available_ = queryDisplays(QDC_ALL_PATHS);
                    refreshSettingsDevices();
                    if (IsWindowVisible(popup_))
                        buildPopup();
                } catch (...) {
                    log(folder_, exceptionText());
                }
            return 0;
        }
        if (w == TimerSettle) {
            KillTimer(owner_, TimerSettle);
            reconcileAudio();
            return 0;
        }
        if (w == TimerSmoke) {
            uiSmokeStep();
            return 0;
        }
        if (w == 5) {
            KillTimer(owner_, 5);
            addTray();
            return 0;
        }
        break;
    case WM_JOB: {
        std::unique_ptr<JobResult> result(reinterpret_cast<JobResult *>(l));
        if (worker_.joinable())
            worker_.join();
        busy_ = false;
        if (settings_)
            EnableWindow(settings_, TRUE);
        try {
            refresh();
        } catch (...) {
            log(folder_, exceptionText());
        }
        queueLayout(popup_);
        queueLayout(settings_);
        auto done = std::move(completion_);
        if (done)
            done(result->error.empty());
        if (!result->error.empty())
            notifyError(result->error);
        return 0;
    }
    case WM_CLOSE:
        if (busy_) {
            notifyError(L"A device switch is still running. Please wait before exiting.");
            return 0;
        }
        DestroyWindow(owner_);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(owner_, message, w, l);
}
} // namespace io
