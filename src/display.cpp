#include "core.h"
#include <cwctype>
#include <sstream>
#include <tuple>

namespace io {
namespace {
using Source = std::tuple<LONG, DWORD, UINT32>;
Source source(const DISPLAYCONFIG_PATH_INFO &p) {
    return {p.sourceInfo.adapterId.HighPart, p.sourceInfo.adapterId.LowPart, p.sourceInfo.id};
}
std::wstring normalized(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return s;
}
bool targetEqual(const std::wstring &a, const std::wstring &b) {
    return !a.empty() && normalized(a) == normalized(b);
}
bool rationalEqual(DISPLAYCONFIG_RATIONAL a, DISPLAYCONFIG_RATIONAL b) {
    return a.Denominator && b.Denominator &&
           uint64_t(a.Numerator) * b.Denominator == uint64_t(b.Numerator) * a.Denominator;
}
} // namespace
Snapshot queryDisplays(UINT32 flags) {
    Snapshot s;
    for (int tries = 0; tries < 5; ++tries) {
        UINT32 np{}, nm{};
        LONG e = GetDisplayConfigBufferSizes(flags, &np, &nm);
        if (e)
            throw std::runtime_error(utf8(L"Read displays: " + errorText(e)));
        if (np > 16384 || nm > 65536)
            throw std::runtime_error("Unexpected number of display paths.");
        s.paths.resize(np);
        s.modes.resize(nm);
        e = QueryDisplayConfig(flags, &np, s.paths.data(), &nm, s.modes.data(), nullptr);
        if (e == ERROR_INSUFFICIENT_BUFFER)
            continue;
        if (e)
            throw std::runtime_error(utf8(L"Read displays: " + errorText(e)));
        s.paths.resize(np);
        s.modes.resize(nm);
        for (auto &p : s.paths) {
            DISPLAYCONFIG_TARGET_DEVICE_NAME n{};
            n.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            n.header.size = sizeof(n);
            n.header.adapterId = p.targetInfo.adapterId;
            n.header.id = p.targetInfo.id;
            auto result = DisplayConfigGetDeviceInfo(&n.header);
            s.targets.push_back(result == ERROR_SUCCESS ? n.monitorDevicePath : L"");
            s.names.push_back(result == ERROR_SUCCESS && n.monitorFriendlyDeviceName[0]
                                  ? n.monitorFriendlyDeviceName
                                  : L"Display");
        }
        if (flags == QDC_ONLY_ACTIVE_PATHS && s.paths.empty())
            throw std::runtime_error("Windows did not report an active display.");
        return s;
    }
    throw std::runtime_error("Displays are changing. Please try again.");
}
bool displaysAvailable(const Snapshot &saved, const Snapshot &available) {
    for (auto &key : saved.targets) {
        bool found = false;
        for (size_t i = 0; i < available.paths.size(); ++i)
            if (available.paths[i].targetInfo.targetAvailable && targetEqual(key, available.targets[i])) {
                found = true;
                break;
            }
        if (!found)
            return false;
    }
    return !saved.paths.empty();
}
Snapshot remapDisplays(const Snapshot &saved, const Snapshot &available) {
    if (saved.paths.empty() || saved.targets.size() != saved.paths.size() ||
        available.targets.size() != available.paths.size())
        throw std::runtime_error("Invalid display preset.");
    std::map<Source, std::vector<size_t>> groups;
    for (size_t i = 0; i < saved.paths.size(); ++i) {
        auto &p = saved.paths[i];
        if (p.sourceInfo.modeInfoIdx >= saved.modes.size() || p.targetInfo.modeInfoIdx >= saved.modes.size())
            throw std::runtime_error("Display preset has an invalid mode.");
        groups[source(p)].push_back(i);
    }
    std::map<Source, Source> routes;
    std::map<Source, std::vector<Source>> candidates;
    for (auto &[old, indices] : groups) {
        std::set<Source> common;
        bool first = true;
        for (auto index : indices) {
            std::set<Source> options;
            for (size_t j = 0; j < available.paths.size(); ++j) {
                auto &live = available.paths[j];
                if (live.targetInfo.targetAvailable &&
                    targetEqual(saved.targets[index], available.targets[j]))
                    options.insert(source(live));
            }
            if (first) {
                common = options;
                first = false;
            } else {
                std::set<Source> both;
                std::set_intersection(common.begin(), common.end(), options.begin(), options.end(),
                                      std::inserter(both, both.end()));
                common = std::move(both);
            }
        }
        if (common.empty())
            throw std::runtime_error("A display in this preset is disconnected or no longer has a compatible "
                                     "connection. Reconnect it or capture the preset again.");
        auto &options = candidates[old];
        options.assign(common.begin(), common.end());
        std::stable_sort(options.begin(), options.end(), [&](const Source &a, const Source &b) {
            return (std::get<2>(a) == std::get<2>(old)) > (std::get<2>(b) == std::get<2>(old));
        });
    }
    // A greedy assignment can consume the only source route available to a later screen.
    std::map<Source, Source> assigned;
    std::function<bool(const Source &, std::set<Source> &)> assign = [&](const Source &group,
                                                                         std::set<Source> &visited) {
        for (const auto &candidate : candidates.at(group)) {
            if (!visited.insert(candidate).second)
                continue;
            auto occupied = assigned.find(candidate);
            if (occupied == assigned.end() || assign(occupied->second, visited)) {
                assigned[candidate] = group;
                routes[group] = candidate;
                return true;
            }
        }
        return false;
    };
    for (const auto &[group, indices] : groups) {
        (void)indices;
        std::set<Source> visited;
        if (!assign(group, visited))
            throw std::runtime_error("Windows cannot route all the screens in this preset at the same time.");
    }
    Snapshot out;
    std::map<Source, UINT32> sourceModes;
    for (size_t i = 0; i < saved.paths.size(); ++i) {
        auto original = saved.paths[i];
        auto route = routes.at(source(original));
        size_t match = available.paths.size();
        for (size_t j = 0; j < available.paths.size(); ++j)
            if (available.paths[j].targetInfo.targetAvailable && source(available.paths[j]) == route &&
                targetEqual(saved.targets[i], available.targets[j])) {
                match = j;
                break;
            }
        if (match == available.paths.size())
            throw std::runtime_error("Display connection changed while switching.");
        auto live = available.paths[match];
        auto p = original;
        p.flags = DISPLAYCONFIG_PATH_ACTIVE;
        p.sourceInfo.adapterId = live.sourceInfo.adapterId;
        p.sourceInfo.id = live.sourceInfo.id;
        p.sourceInfo.statusFlags = 0;
        p.targetInfo.adapterId = live.targetInfo.adapterId;
        p.targetInfo.id = live.targetInfo.id;
        p.targetInfo.targetAvailable = TRUE;
        p.targetInfo.statusFlags = 0;
        p.targetInfo.outputTechnology = live.targetInfo.outputTechnology;
        if (!sourceModes.contains(route)) {
            auto m = saved.modes[original.sourceInfo.modeInfoIdx];
            if (m.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
                throw std::runtime_error("Invalid source mode.");
            m.id = p.sourceInfo.id;
            m.adapterId = p.sourceInfo.adapterId;
            sourceModes[route] = static_cast<UINT32>(out.modes.size());
            out.modes.push_back(m);
        }
        p.sourceInfo.modeInfoIdx = sourceModes.at(route);
        auto target = saved.modes[original.targetInfo.modeInfoIdx];
        if (target.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
            throw std::runtime_error("Invalid target mode.");
        target.id = p.targetInfo.id;
        target.adapterId = p.targetInfo.adapterId;
        p.targetInfo.modeInfoIdx = static_cast<UINT32>(out.modes.size());
        out.modes.push_back(target);
        out.paths.push_back(p);
        out.targets.push_back(available.targets[match]);
        out.names.push_back(saved.names.size() > i ? saved.names[i] : L"Display");
    }
    return out;
}
bool sameDisplays(const Snapshot &a, const Snapshot &b) {
    if (a.paths.size() != b.paths.size() || a.targets.size() != a.paths.size() ||
        b.targets.size() != b.paths.size())
        return false;
    for (size_t i = 0; i < a.paths.size(); ++i) {
        size_t j = 0;
        while (j < b.paths.size() && !targetEqual(a.targets[i], b.targets[j]))
            ++j;
        if (j == b.paths.size())
            return false;
        auto &ap = a.paths[i];
        auto &bp = b.paths[j];
        if (ap.sourceInfo.modeInfoIdx >= a.modes.size() || bp.sourceInfo.modeInfoIdx >= b.modes.size())
            return false;
        auto &am = a.modes[ap.sourceInfo.modeInfoIdx].sourceMode;
        auto &bm = b.modes[bp.sourceInfo.modeInfoIdx].sourceMode;
        if (am.width != bm.width || am.height != bm.height || am.position.x != bm.position.x ||
            am.position.y != bm.position.y || am.pixelFormat != bm.pixelFormat ||
            ap.targetInfo.rotation != bp.targetInfo.rotation ||
            !rationalEqual(ap.targetInfo.refreshRate, bp.targetInfo.refreshRate))
            return false;
        for (size_t k = 0; k < a.paths.size(); ++k) {
            size_t l = 0;
            while (l < b.paths.size() && !targetEqual(a.targets[k], b.targets[l]))
                ++l;
            if (l == b.paths.size() ||
                (source(ap) == source(a.paths[k])) != (source(bp) == source(b.paths[l])))
                return false;
        }
    }
    return true;
}
std::wstring describeDisplays(const Snapshot &s) {
    std::wostringstream out;
    for (size_t i = 0; i < s.paths.size(); ++i) {
        auto &p = s.paths[i];
        if (p.sourceInfo.modeInfoIdx >= s.modes.size())
            continue;
        auto &m = s.modes[p.sourceInfo.modeInfoIdx].sourceMode;
        if (i)
            out << L"\r\n";
        out << (i < s.names.size() ? s.names[i] : L"Display") << L" · " << m.width << L" × " << m.height;
        if (p.targetInfo.refreshRate.Denominator) {
            double hz = double(p.targetInfo.refreshRate.Numerator) / p.targetInfo.refreshRate.Denominator;
            out << L" · " << static_cast<int>(hz + 0.5) << L" Hz";
        }
        out << L" · (" << m.position.x << L", " << m.position.y << L")";
        if (m.position.x == 0 && m.position.y == 0)
            out << L" · Primary";
    }
    return out.str();
}
void applyDisplays(const Snapshot &saved) {
    auto planned = remapDisplays(saved, queryDisplays(QDC_ALL_PATHS));
    auto flags = SDC_USE_SUPPLIED_DISPLAY_CONFIG;
    LONG e = SetDisplayConfig(static_cast<UINT32>(planned.paths.size()), planned.paths.data(),
                              static_cast<UINT32>(planned.modes.size()), planned.modes.data(),
                              flags | SDC_VALIDATE);
    if (e)
        throw std::runtime_error(utf8(L"Windows could not validate this display preset: " + errorText(e) +
                                      L" Capture it again using the current connections."));
    e = SetDisplayConfig(static_cast<UINT32>(planned.paths.size()), planned.paths.data(),
                         static_cast<UINT32>(planned.modes.size()), planned.modes.data(),
                         flags | SDC_APPLY | SDC_SAVE_TO_DATABASE);
    if (e)
        throw std::runtime_error(utf8(L"Windows could not apply the display preset: " + errorText(e)));
    if (!sameDisplays(saved, queryDisplays()))
        throw std::runtime_error(
            "Windows applied a different display layout. The previous setup will be restored.");
}
void applyAndKeepDisplay(const std::function<void()> &apply, HANDLE keep, HANDLE revert) {
    try {
        apply(); // Includes reading back and verifying the requested layout.
        if (!SetEvent(keep))
            throw std::runtime_error("Could not finish display recovery protection.");
    } catch (...) {
        SetEvent(revert);
        throw;
    }
}
bool recoveryRequired(HANDLE keep, HANDLE revert, HANDLE owner, DWORD applyingMs) {
    HANDLE handles[] = {keep, revert, owner};
    auto result = WaitForMultipleObjects(3, handles, FALSE, applyingMs);
    return result != WAIT_OBJECT_0;
}
int watchdog(const std::filesystem::path &path, const std::wstring &eventName, DWORD parent) {
    ComScope com;
    Handle keep(OpenEventW(SYNCHRONIZE, FALSE, (eventName + L".keep").c_str()));
    Handle revert(OpenEventW(SYNCHRONIZE, FALSE, (eventName + L".revert").c_str()));
    Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, (eventName + L".ready").c_str()));
    Handle owner(OpenProcess(SYNCHRONIZE, FALSE, parent));
    if (!keep || !revert || !ready || !owner)
        return 2;
    try {
        auto previous = loadSnapshot(path);
        SetEvent(ready.get());
        if (!recoveryRequired(keep.get(), revert.get(), owner.get())) {
            DeleteFileW(path.c_str());
            return 0;
        }
        applyDisplays(previous);
        log(path.parent_path(), L"Restored previous display setup.");
        DeleteFileW(path.c_str());
        return 1; // Distinguish recovery from successfully keeping the requested layout.
    } catch (const std::exception &e) {
        log(path.parent_path(), L"Display recovery failed: " + wide(e.what()));
        return 3;
    }
}
} // namespace io
