#pragma once
#include "core.h"
#include "theme.h"
#include <commctrl.h>
#include <functional>
#include <memory>

namespace io {
constexpr UINT WM_TRAY = WM_APP + 1, WM_DEVICES = WM_APP + 2, WM_JOB = WM_APP + 3, WM_RELAYOUT = WM_APP + 5;
struct WindowUi {
    UINT dpi = 96;
    HFONT font{}, smallFont{}, titleFont{}, iconFont{};
    bool layoutQueued = false;
    int scale(int n) const {
        return MulDiv(n, static_cast<int>(dpi), 96);
    }
};
struct JobResult {
    std::wstring error;
};
class DeviceNotifications final : public IMMNotificationClient {
    std::atomic<ULONG> refs_{1};
    HWND hwnd_;

  public:
    explicit DeviceNotifications(HWND hwnd) : hwnd_(hwnd) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override;
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs_;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        PostMessageW(hwnd_, WM_DEVICES, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
        PostMessageW(hwnd_, WM_DEVICES, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
        PostMessageW(hwnd_, WM_DEVICES, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override {
        PostMessageW(hwnd_, WM_DEVICES, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        PostMessageW(hwnd_, WM_DEVICES, 0, 0);
        return S_OK;
    }
};
class App {
    HINSTANCE instance_;
    std::filesystem::path folder_;
    bool noStartup_, smoke_, uiSmoke_;
    Config config_;
    bool readOnly_ = false;
    HWND owner_{}, popup_{}, settings_{}, navigation_{}, list_{}, name_{}, details_{}, fallback_{},
        startup_{}, calls_{}, popupTips_{};
    WindowUi popupUi_, settingsUi_;
    std::map<UINT, std::vector<HFONT>> fontCache_;
    Theme theme_;
    HICON icon_{};
    UINT taskbarCreated_{};
    NOTIFYICONDATAW tray_{};
    ComPtr<IMMDeviceEnumerator> enumerator_;
    DeviceNotifications *notifications_{};
    Snapshot current_, available_;
    std::vector<AudioDevice> outputs_, inputs_;
    std::wstring currentOutput_, currentInput_, wantedOutput_, wantedInput_;
    std::wstring notice_;
    bool busy_ = false, updating_ = false;
    std::thread worker_;
    std::function<void(bool)> completion_;
    ULONGLONG preserveUntil_{};
    unsigned preserveAttempts_{};
    HANDLE keep_{}, revert_{}, ready_{}, guardian_{};
    std::filesystem::path recovery_;
    int page_ = 0, selected_ = 0, popupScroll_ = 0, popupContent_ = 0, popupHeight_ = 0;
    struct Row {
        std::wstring label;
        bool selected = false;
    };
    std::map<HWND, Row> rows_;
    std::vector<std::pair<HWND, int>> popupChildren_;
    std::vector<HWND> settingChildren_;
    std::vector<RECT> settingsCards_;
    std::vector<AudioDevice> settingsDevices_;
    HWND control(HWND parent, const wchar_t *cls, const std::wstring &text, DWORD style, int x, int y, int w,
                 int h, int id = 0);
    void addTray();
    void removeTray();
    void refresh(bool display = true);
    void notifyError(const std::wstring &text);
    bool save(const Config &before);
    void startJob(std::function<void()> action, std::function<void(bool)> completion);
    void switchDisplay(size_t index);
    void switchAudio(const std::wstring &id, EDataFlow flow);
    void beginGuardian(const Snapshot &previous);
    void closeGuardian();
    void reconcileAudio();
    void buildPopup(bool syncDpi = true);
    POINT popupAnchor() const;
    void syncPopupDpi();
    void showPopup();
    void positionPopup();
    void scrollPopup(int delta);
    void drawButton(const DRAWITEMSTRUCT &item);
    void drawPopupHeader(const DRAWITEMSTRUCT &item);
    void drawNavigation(const DRAWITEMSTRUCT &item);
    COLORREF settingsBackground(HWND child) const;
    LRESULT drawList(NMHDR *notification);
    void showSettings(int page = -1);
    void buildSettings();
    void relayoutSettings(bool preserveDraft = true);
    void populateList();
    void refreshSettingsDevices();
    void selectSettings(int index);
    void settingsCommand(int id);
    void settingsCheck(NMLISTVIEW *changed);
    void updateFallback();
    std::vector<AudioChoice> &choices() {
        return page_ == 1 ? config_.outputs : config_.inputs;
    }
    void openWindowsSettings(const wchar_t *uri);
    void recoverAtLaunch();
    void createFonts(WindowUi &ui, UINT dpi);
    void updateWindowDpi(HWND window, UINT dpi);
    void queueLayout(HWND window);
    void uiSmokeStep();

  public:
    App(HINSTANCE instance, std::filesystem::path folder, bool noStartup, bool smoke, bool uiSmoke)
        : instance_(instance), folder_(std::move(folder)), noStartup_(noStartup), smoke_(smoke),
          uiSmoke_(uiSmoke) {}
    ~App();
    int run(bool showSettings);
    LRESULT ownerMessage(UINT, WPARAM, LPARAM);
    LRESULT popupMessage(UINT, WPARAM, LPARAM);
    LRESULT settingsMessage(UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK ownerProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK popupProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK settingsProc(HWND, UINT, WPARAM, LPARAM);
};
} // namespace io
