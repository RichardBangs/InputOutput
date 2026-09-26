#include "core.h"
#include <psapi.h>

namespace io {
namespace {
Snapshot fixture(unsigned screens = 2, bool clone = false) {
    Snapshot s;
    for (unsigned i = 0; i < screens; ++i) {
        DISPLAYCONFIG_PATH_INFO p{};
        p.flags = DISPLAYCONFIG_PATH_ACTIVE;
        p.sourceInfo.adapterId.LowPart = 1;
        p.sourceInfo.id = clone ? 0 : i;
        p.targetInfo.adapterId.LowPart = 1;
        p.targetInfo.id = 10 + i;
        p.targetInfo.targetAvailable = TRUE;
        p.targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
        p.targetInfo.scaling = DISPLAYCONFIG_SCALING_IDENTITY;
        p.targetInfo.refreshRate = {60000, 1000};
        p.targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
        p.targetInfo.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
        DISPLAYCONFIG_MODE_INFO source{};
        source.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
        source.id = p.sourceInfo.id;
        source.adapterId = p.sourceInfo.adapterId;
        source.sourceMode.width = 1920;
        source.sourceMode.height = 1080;
        source.sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
        source.sourceMode.position.x = clone ? 0 : static_cast<LONG>(i * 1920);
        if (clone && i)
            p.sourceInfo.modeInfoIdx = 0;
        else {
            p.sourceInfo.modeInfoIdx = static_cast<UINT32>(s.modes.size());
            s.modes.push_back(source);
        }
        DISPLAYCONFIG_MODE_INFO target{};
        target.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
        target.id = p.targetInfo.id;
        target.adapterId = p.targetInfo.adapterId;
        target.targetMode.targetVideoSignalInfo.activeSize = {1920, 1080};
        target.targetMode.targetVideoSignalInfo.vSyncFreq = {60000, 1000};
        p.targetInfo.modeInfoIdx = static_cast<UINT32>(s.modes.size());
        s.modes.push_back(target);
        s.paths.push_back(p);
        s.targets.push_back(L"\\\\?\\DISPLAY#TEST" + std::to_wstring(i));
        s.names.push_back(L"Test monitor " + std::to_wstring(i + 1));
    }
    return s;
}
std::vector<AudioDevice> hdmiFixture() {
    return {{L"old-tv", L"3 - LG TV SSCR2 (AMD audio)", eRender, DEVICE_STATE_UNPLUGGED, L"3 - LG TV SSCR2",
             L"graphics-container", L"AMD audio", true},
            {L"new-tv", L"2 - LG TV SSCR2 (AMD audio)", eRender, DEVICE_STATE_ACTIVE, L"2 - LG TV SSCR2",
             L"graphics-container", L"AMD audio", true}};
}
} // namespace
int runSelfTests(const std::filesystem::path &folder) {
    std::filesystem::create_directories(folder);
    std::ofstream report(folder / L"self-test.txt");
    int passed = 0, failed = 0;
    auto test = [&](const char *name, std::function<void()> body) {
        try {
            body();
            report << "PASS " << name << "\n";
            ++passed;
        } catch (const std::exception &e) {
            report << "FAIL " << name << ": " << e.what() << "\n";
            ++failed;
        }
    };
    auto require = [](bool value) {
        if (!value)
            throw std::runtime_error("Assertion failed");
    };
    test("Unicode settings and exact display data round trip", [&] {
        Config c;
        c.displays.push_back({L"one", L"TV — 夜 🎧", true, false, fixture()});
        c.outputs.push_back({L"endpoint-output", L"Speakers", true});
        c.inputs.push_back({L"endpoint-mic", L"Microphone", false});
        c.outputFallback = L"endpoint-output";
        c.calls = false;
        auto bytes = encodeConfig(c);
        auto read = decodeConfig(bytes);
        require(read.displays[0].name == c.displays[0].name);
        require(read.inputs[0].shown == false);
        require(!read.calls);
        require(read.outputFallback == c.outputFallback);
        require(sameDisplays(read.displays[0].snapshot, c.displays[0].snapshot));
        require(encodeConfig(read) == bytes);
    });
    test("Every truncated settings file is rejected", [&] {
        Config c;
        c.displays.push_back({L"one", L"Desktop", true, false, fixture()});
        auto bytes = encodeConfig(c);
        for (size_t n = 0; n < bytes.size(); ++n) {
            auto truncated = std::vector<uint8_t>(bytes.begin(), bytes.begin() + n);
            bool caught = false;
            try {
                decodeConfig(truncated);
            } catch (...) {
                caught = true;
            }
            require(caught);
        }
    });
    test("Single-byte corruption is rejected", [&] {
        auto bytes = encodeConfig(Config{});
        for (size_t i = 0; i < bytes.size(); ++i) {
            auto corrupt = bytes;
            corrupt[i] ^= 0x40;
            bool caught = false;
            try {
                decodeConfig(corrupt);
            } catch (...) {
                caught = true;
            }
            require(caught);
        }
    });
    test("Invalid mode indices cannot be saved", [&] {
        Config c;
        auto s = fixture();
        s.paths[0].sourceInfo.modeInfoIdx = 999;
        c.displays.push_back({L"bad", L"Bad", true, false, s});
        bool caught = false;
        try {
            encodeConfig(c);
        } catch (...) {
            caught = true;
        }
        require(caught);
    });
    test("Duplicate monitor identities cannot be saved", [&] {
        Config c;
        auto s = fixture();
        s.targets[1] = s.targets[0];
        c.displays.push_back({L"bad", L"Bad", true, false, s});
        bool caught = false;
        try {
            encodeConfig(c);
        } catch (...) {
            caught = true;
        }
        require(caught);
    });
    test("Display identities survive adapter and target renumbering", [&] {
        auto saved = fixture();
        auto live = saved;
        for (size_t i = 0; i < live.paths.size(); ++i) {
            auto &p = live.paths[i];
            p.sourceInfo.adapterId.LowPart = 55;
            p.targetInfo.adapterId.LowPart = 55;
            p.sourceInfo.id += 4;
            p.targetInfo.id += 30;
        }
        auto plan = remapDisplays(saved, live);
        require(sameDisplays(saved, plan));
        require(plan.paths[0].sourceInfo.adapterId.LowPart == 55);
        require(plan.paths[0].targetInfo.id == 40);
        require(plan.modes[plan.paths[0].sourceInfo.modeInfoIdx].id == 4);
        require(plan.modes[plan.paths[0].targetInfo.modeInfoIdx].id == 40);
    });
    test("Source routing reassigns an earlier screen when needed", [&] {
        auto saved = fixture();
        auto live = saved;
        live.paths[1].sourceInfo.id = 0;
        auto alternate = live.paths[0];
        alternate.sourceInfo.id = 1;
        live.paths.push_back(alternate);
        live.targets.push_back(live.targets[0]);
        live.names.push_back(live.names[0]);
        auto plan = remapDisplays(saved, live);
        require(plan.paths[0].sourceInfo.id == 1);
        require(plan.paths[1].sourceInfo.id == 0);
        require(sameDisplays(saved, plan));
    });
    test("Missing monitor refuses the entire layout", [&] {
        auto saved = fixture();
        auto live = fixture(1);
        bool caught = false;
        try {
            remapDisplays(saved, live);
        } catch (...) {
            caught = true;
        }
        require(caught);
        require(!displaysAvailable(saved, live));
    });
    test("Cloned displays reuse exactly one source mode", [&] {
        auto saved = fixture(2, true);
        auto live = saved;
        for (auto &p : live.paths) {
            p.sourceInfo.id = 8;
            p.targetInfo.id += 20;
        }
        auto plan = remapDisplays(saved, live);
        require(plan.paths[0].sourceInfo.modeInfoIdx == plan.paths[1].sourceInfo.modeInfoIdx);
        require(sameDisplays(saved, plan));
    });
    test("Display matching ignores enumeration order", [&] {
        auto saved = fixture();
        auto reversed = saved;
        std::reverse(reversed.paths.begin(), reversed.paths.end());
        std::reverse(reversed.targets.begin(), reversed.targets.end());
        std::reverse(reversed.names.begin(), reversed.names.end());
        require(sameDisplays(saved, reversed));
    });
    test("Primary position and refresh changes are detected", [&] {
        auto s = fixture();
        auto changed = s;
        changed.modes[0].sourceMode.position.x = -1920;
        require(!sameDisplays(s, changed));
        changed = s;
        changed.paths[0].targetInfo.refreshRate = {120000, 1000};
        require(!sameDisplays(s, changed));
        changed = s;
        changed.paths[0].targetInfo.refreshRate = {60, 1};
        require(sameDisplays(s, changed));
    });
    test("Disabled endpoints are not selectable", [&] {
        std::vector<AudioDevice> devices = {{L"one", L"One", eRender, DEVICE_STATE_DISABLED},
                                            {L"two", L"Two", eCapture, DEVICE_STATE_ACTIVE}};
        require(!audioAvailable(devices, L"one"));
        require(audioAvailable(devices, L"two"));
        require(!audioAvailable(devices, L"missing"));
    });
    test("Renumbered HDMI output preserves menu labels, visibility, order and fallback", [&] {
        Config c;
        c.outputs = {{L"speakers", L"Speakers", true},
                     {L"old-tv", L"Living room TV", false},
                     {L"headphones", L"Headphones", true}};
        c.inputs = {{L"mic", L"Microphone", true}};
        c.outputFallback = L"old-tv";
        c.inputFallback = L"mic";
        c.displays.push_back({L"tv", L"TV", true, false, fixture(1)});
        c.startup = c.calls = false;
        auto expected = c;
        expected.outputs[1].id = expected.outputFallback = L"new-tv";
        auto devices = hdmiFixture();
        auto changes = reconnectAudioChoices(c, devices);
        require(changes.size() == 1 && changes.at(L"old-tv") == L"new-tv");
        require(audioAvailable(devices, c.outputs[1].id));
        require(encodeConfig(c) == encodeConfig(expected));
        require(reconnectAudioChoices(c, devices).empty());
        auto data = folder / L"hdmi-reconnect";
        saveConfig(data, expected);
        require(encodeConfig(loadConfig(data)) == encodeConfig(expected));
    });
    test("TV power cycle reconnects a saved endpoint again without creating another choice", [&] {
        Config c;
        c.outputs = {{L"old-tv", L"TV speakers", true}};
        auto devices = hdmiFixture();
        devices[1].state = DEVICE_STATE_NOTPRESENT;
        require(reconnectAudioChoices(c, devices).empty());
        require(!audioAvailable(devices, c.outputs[0].id));
        devices[1].state = DEVICE_STATE_ACTIVE;
        require(reconnectAudioChoices(c, devices).size() == 1);
        devices[1].state = DEVICE_STATE_NOTPRESENT;
        devices[0].state = DEVICE_STATE_ACTIVE;
        require(reconnectAudioChoices(c, devices).size() == 1);
        require(c.outputs.size() == 1 && c.outputs[0].id == L"old-tv");
        require(c.outputs[0].name == L"TV speakers");
    });
    test("Multiple active HDMI matches leave a saved choice disconnected", [&] {
        Config c;
        c.outputs = {{L"old-tv", L"TV", true}};
        auto devices = hdmiFixture();
        auto duplicate = devices[1];
        duplicate.id = L"second-tv";
        devices.push_back(duplicate);
        require(reconnectAudioChoices(c, devices).empty());
        require(c.outputs[0].id == L"old-tv");
    });
    test("Two saved HDMI choices cannot claim the same replacement in either menu order", [&] {
        Config c;
        c.outputs = {{L"old-tv", L"First TV", true}, {L"second-old-tv", L"Second TV", false}};
        auto devices = hdmiFixture();
        auto duplicate = devices[0];
        duplicate.id = L"second-old-tv";
        devices.push_back(duplicate);
        for (int order = 0; order < 2; ++order) {
            auto before = encodeConfig(c);
            require(reconnectAudioChoices(c, devices).empty());
            require(encodeConfig(c) == before);
            std::reverse(c.outputs.begin(), c.outputs.end());
        }
    });
    test("An HDMI endpoint already configured separately is never claimed", [&] {
        Config c;
        c.outputs = {{L"old-tv", L"Old TV", true}, {L"new-tv", L"New TV", false}};
        auto before = encodeConfig(c);
        require(reconnectAudioChoices(c, hdmiFixture()).empty());
        require(encodeConfig(c) == before);
    });
    const char *hdmiRejections[] = {"Active HDMI IDs are retained",
                                    "Disabled HDMI IDs are retained",
                                    "Missing old HDMI hardware metadata is not guessed from a menu name",
                                    "Different HDMI device models are not interchangeable",
                                    "Different HDMI hardware containers are not interchangeable",
                                    "Different audio controllers are not interchangeable",
                                    "Empty HDMI containers cannot identify a replacement",
                                    "Empty HDMI descriptions cannot identify a replacement",
                                    "Empty audio controllers cannot identify a replacement",
                                    "Non-HDMI endpoints are not rebound",
                                    "Microphones are not rebound",
                                    "Disconnected replacements are not selectable"};
    for (int scenario = 0; scenario < static_cast<int>(std::size(hdmiRejections)); ++scenario)
        test(hdmiRejections[scenario], [&, scenario] {
            Config c;
            c.outputs = {{L"old-tv", L"2 - LG TV SSCR2 (AMD audio)", true}};
            auto before = encodeConfig(c);
            auto devices = hdmiFixture();
            switch (scenario) {
            case 0:
                devices[0].state = DEVICE_STATE_ACTIVE;
                break;
            case 1:
                devices[0].state = DEVICE_STATE_DISABLED;
                break;
            case 2:
                devices.erase(devices.begin());
                break;
            case 3:
                devices[1].description = L"2 - Different TV";
                break;
            case 4:
                devices[1].containerId = L"other-container";
                break;
            case 5:
                devices[1].controller = L"other-controller";
                break;
            case 6:
                devices[0].containerId = devices[1].containerId = L"";
                break;
            case 7:
                devices[0].description = devices[1].description = L"";
                break;
            case 8:
                devices[0].controller = devices[1].controller = L"";
                break;
            case 9:
                devices[0].hdmi = devices[1].hdmi = false;
                break;
            case 10:
                devices[0].flow = devices[1].flow = eCapture;
                break;
            case 11:
                devices[1].state = DEVICE_STATE_UNPLUGGED;
                break;
            }
            require(reconnectAudioChoices(c, devices).empty());
            require(encodeConfig(c) == before);
        });
    test("Atomic persistence retains previous backup", [&] {
        auto data = folder / L"persistence";
        Config a;
        a.calls = false;
        saveConfig(data, a);
        Config b;
        b.calls = true;
        saveConfig(data, b);
        require(loadConfig(data).calls);
        require(!decodeConfig(readBytes(data / L"settings.dat.bak")).calls);
    });
    test("Recovery snapshots retain complete layouts", [&] {
        auto p = folder / L"recovery-test.dat";
        auto s = fixture();
        saveSnapshot(p, s);
        require(sameDisplays(s, loadSnapshot(p)));
    });
    for (int scenario = 0; scenario < 4; ++scenario) {
        const char *labels[] = {"Keep cancels recovery", "Explicit revert requests recovery",
                                "Parent exit requests recovery", "Hung application requests recovery"};
        test(labels[scenario], [&, scenario] {
            Handle keep(CreateEventW(nullptr, TRUE, scenario == 0, nullptr));
            Handle revert(CreateEventW(nullptr, TRUE, scenario == 1, nullptr));
            Handle owner(CreateEventW(nullptr, TRUE, scenario == 2, nullptr));
            require(recoveryRequired(keep.get(), revert.get(), owner.get(), 2) == (scenario != 0));
        });
    }
    test("Verified display switch automatically cancels recovery", [&] {
        Handle keep(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Handle revert(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Handle owner(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        bool applied = false;
        applyAndKeepDisplay(
            [&] {
                require(WaitForSingleObject(keep.get(), 0) == WAIT_TIMEOUT);
                applied = true;
            },
            keep.get(), revert.get());
        require(applied);
        require(WaitForSingleObject(keep.get(), 0) == WAIT_OBJECT_0);
        require(WaitForSingleObject(revert.get(), 0) == WAIT_TIMEOUT);
        require(!recoveryRequired(keep.get(), revert.get(), owner.get(), 2));
    });
    test("Failed display verification requests recovery and preserves the error", [&] {
        Handle keep(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Handle revert(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        bool caught = false;
        try {
            applyAndKeepDisplay([] { throw std::runtime_error("verification failed"); }, keep.get(),
                                revert.get());
        } catch (const std::exception &e) {
            caught = std::string(e.what()) == "verification failed";
        }
        require(caught);
        require(WaitForSingleObject(keep.get(), 0) == WAIT_TIMEOUT);
        require(WaitForSingleObject(revert.get(), 0) == WAIT_OBJECT_0);
    });
    test("Completed display switch remains kept after the parent exits", [&] {
        Handle keep(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Handle revert(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Handle owner(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        applyAndKeepDisplay([] {}, keep.get(), revert.get());
        SetEvent(owner.get());
        require(!recoveryRequired(keep.get(), revert.get(), owner.get(), 2));
    });
    test("Successful display transaction stops the separate recovery process", [&] {
        auto path = folder / L"guardian-test.dat";
        saveSnapshot(path, fixture());
        auto event = L"Local\\InputOutput.Test." + newId();
        Handle keep(CreateEventW(nullptr, TRUE, FALSE, (event + L".keep").c_str()));
        Handle revert(CreateEventW(nullptr, TRUE, FALSE, (event + L".revert").c_str()));
        Handle ready(CreateEventW(nullptr, TRUE, FALSE, (event + L".ready").c_str()));
        auto command = L"\"" + executablePath() + L"\" --watchdog \"" + path.wstring() + L"\" \"" + event +
                       L"\" " + std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                               nullptr, &startup, &child));
        Handle process(child.hProcess), thread(child.hThread);
        require(WaitForSingleObject(ready.get(), 5000) == WAIT_OBJECT_0);
        require(WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT);
        applyAndKeepDisplay([] {}, keep.get(), revert.get());
        require(WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0);
        DWORD exitCode{};
        require(GetExitCodeProcess(process.get(), &exitCode) && exitCode == 0);
        require(!std::filesystem::exists(path));
    });
    report << "\n" << passed << " passed; " << failed << " failed.\n";
    return failed ? 1 : 0;
}
void diagnose(const std::filesystem::path &folder) {
    std::filesystem::create_directories(folder);
    std::ofstream out(folder / L"diagnostics.txt");
    out << "InputOutput 0.1.0 diagnostics (read only)\n";
    auto current = queryDisplays();
    auto available = queryDisplays(QDC_ALL_PATHS);
    out << "\nActive displays: " << current.paths.size() << "\n"
        << utf8(describeDisplays(current)) << "\nAvailable display paths: " << available.paths.size() << "\n";
    auto remapped = remapDisplays(current, available);
    out << "Current layout remap equivalent: " << (sameDisplays(current, remapped) ? "yes" : "NO") << "\n";
    LONG valid = SetDisplayConfig(static_cast<UINT32>(remapped.paths.size()), remapped.paths.data(),
                                  static_cast<UINT32>(remapped.modes.size()), remapped.modes.data(),
                                  SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_VALIDATE);
    out << "Read-only display validation: " << valid << "\n";
    for (auto flow : {eRender, eCapture}) {
        out << (flow == eRender ? "\nAudio outputs:\n" : "\nMicrophones:\n");
        auto selected = defaultAudio(flow);
        for (auto &device : audioDevices(flow))
            out << (device.id == selected ? "* " : "  ") << utf8(device.name) << " ["
                << utf8(deviceState(device.state)) << "]\n";
    }
    if (std::filesystem::exists(folder / L"settings.dat")) {
        auto config = loadConfig(folder);
        auto devices = audioDevices(eRender);
        auto changes = reconnectAudioChoices(config, devices);
        out << "\nSaved audio outputs (read-only reconnection preview):\n";
        for (const auto &choice : config.outputs) {
            auto found =
                std::find_if(devices.begin(), devices.end(), [&](auto &d) { return d.id == choice.id; });
            out << "  " << utf8(choice.name) << " ["
                << utf8(deviceState(found == devices.end() ? DEVICE_STATE_NOTPRESENT : found->state)) << "]\n"
                << "    Endpoint: " << utf8(choice.id) << "\n";
        }
        for (const auto &[oldId, newId] : changes)
            out << "  Would reconnect: " << utf8(oldId) << " -> " << utf8(newId) << "\n";
        out << "  " << changes.size() << " saved choice(s) would reconnect; settings not changed.\n";
    }
    out << "\nAudio policy interface creation: 0x" << std::hex
        << static_cast<unsigned long>(audioPolicyAvailable()) << std::dec << " (no device changed)\n";
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                             sizeof(memory)))
        out << "Private memory: " << memory.PrivateUsage / 1024
            << " KiB\nWorking set: " << memory.WorkingSetSize / 1024 << " KiB\n";
    if (valid)
        throw std::runtime_error("Read-only display validation failed. See diagnostics.txt.");
}
} // namespace io
