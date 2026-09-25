#include "app.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace io;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ComScope com;
    INITCOMMONCONTROLSEX controls{sizeof(controls),
                                  ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_LINK_CLASS};
    InitCommonControlsEx(&controls);
    int count{};
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> args;
    for (int i = 1; i < count; ++i)
        args.emplace_back(argv[i]);
    LocalFree(argv);
    std::filesystem::path folder;
    bool noStartup = false, settings = false, smoke = false, uiSmoke = false, selfTest = false,
         diagnostics = false;
    try {
        if (args.size() == 4 && args[0] == L"--watchdog")
            return watchdog(args[1], args[2], std::stoul(args[3]));
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == L"--data-dir" && i + 1 < args.size())
                folder = args[++i];
            else if (args[i] == L"--no-startup")
                noStartup = true;
            else if (args[i] == L"--settings")
                settings = true;
            else if (args[i] == L"--ui-smoke") {
                uiSmoke = true;
                noStartup = true;
            } else if (args[i] == L"--demo") {
                smoke = true;
                noStartup = true;
            } else if (args[i] == L"--self-test") {
                selfTest = true;
                noStartup = true;
            } else if (args[i] == L"--diagnose") {
                diagnostics = true;
                noStartup = true;
            } else if (args[i] != L"--background")
                throw std::runtime_error("Unknown command-line option.");
        }
        if (folder.empty()) {
            if (selfTest || uiSmoke || smoke)
                throw std::runtime_error("Test modes require an explicit --data-dir.");
            PWSTR appData = nullptr;
            check(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData), L"Find settings folder");
            folder = std::filesystem::path(appData) / L"InputOutput";
            CoTaskMemFree(appData);
        }
        folder = std::filesystem::absolute(folder);
        if (selfTest)
            return runSelfTests(folder);
        if (diagnostics) {
            diagnose(folder);
            return 0;
        }
        auto ownerTitle = L"InputOutput." + std::to_wstring(std::hash<std::wstring>{}(folder.wstring()));
        std::wstring mutexName = L"Local\\" + ownerTitle;
        Handle mutex(CreateMutexW(nullptr, FALSE, mutexName.c_str()));
        if (!mutex)
            throw std::runtime_error("Could not create the app's single-instance lock.");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            auto window = FindWindowW(L"InputOutput.Owner", ownerTitle.c_str());
            if (window)
                PostMessageW(window, WM_APP + 4, 0, 0);
            return 0;
        }
        App app(instance, folder, noStartup, smoke, uiSmoke);
        return app.run(settings);
    } catch (const std::exception &e) {
        if (!folder.empty())
            log(folder, wide(e.what()));
        if (selfTest || diagnostics || uiSmoke)
            return 1;
        MessageBoxW(nullptr, wide(e.what()).c_str(), L"InputOutput", MB_OK | MB_ICONERROR);
        return 1;
    }
}
