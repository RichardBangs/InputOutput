#include "core.h"
#include <audioclient.h>

namespace io {
namespace {
// Windows' default-endpoint policy interface is undocumented. Keep its ABI isolated here.
// Method order follows IPolicyConfig (Vista's interface has a different IID/layout).
struct IPolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX *, WAVEFORMATEX *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
constexpr GUID PolicyClass = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
constexpr GUID PolicyInterface = {
    0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};
void enumerator(ComPtr<IMMDeviceEnumerator> &e) {
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                           __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(e.put())),
          L"Read audio devices");
}
std::wstring deviceId(IMMDevice *d) {
    wchar_t *id = nullptr;
    check(d->GetId(&id), L"Read device identifier");
    std::wstring result = id;
    CoTaskMemFree(id);
    return result;
}
} // namespace
std::vector<AudioDevice> audioDevices(EDataFlow flow) {
    ComPtr<IMMDeviceEnumerator> e;
    enumerator(e);
    ComPtr<IMMDeviceCollection> devices;
    check(e->EnumAudioEndpoints(flow, DEVICE_STATEMASK_ALL, devices.put()), L"List audio devices");
    UINT count = 0;
    check(devices->GetCount(&count), L"Count audio devices");
    std::vector<AudioDevice> result;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> d;
        if (FAILED(devices->Item(i, d.put())))
            continue;
        AudioDevice a;
        a.id = deviceId(d.get());
        a.flow = flow;
        if (FAILED(d->GetState(&a.state)))
            continue;
        ComPtr<IPropertyStore> properties;
        if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, properties.put()))) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR &&
                value.pwszVal)
                a.name = value.pwszVal;
            PropVariantClear(&value);
        }
        if (a.name.empty())
            a.name = L"Audio device";
        result.push_back(std::move(a));
    }
    std::stable_sort(result.begin(), result.end(), [](auto &a, auto &b) {
        if ((a.state == DEVICE_STATE_ACTIVE) != (b.state == DEVICE_STATE_ACTIVE))
            return a.state == DEVICE_STATE_ACTIVE;
        return a.name < b.name;
    });
    return result;
}
std::wstring defaultAudio(EDataFlow flow, ERole role) {
    ComPtr<IMMDeviceEnumerator> e;
    enumerator(e);
    ComPtr<IMMDevice> d;
    HRESULT hr = e->GetDefaultAudioEndpoint(flow, role, d.put());
    if (hr == E_NOTFOUND)
        return {};
    check(hr, L"Read default audio device");
    return deviceId(d.get());
}
bool audioAvailable(const std::vector<AudioDevice> &devices, const std::wstring &id) {
    return std::any_of(devices.begin(), devices.end(),
                       [&](auto &d) { return d.id == id && d.state == DEVICE_STATE_ACTIVE; });
}
std::wstring deviceState(DWORD state) {
    if (state == DEVICE_STATE_ACTIVE)
        return L"Available";
    if (state == DEVICE_STATE_DISABLED)
        return L"Disabled in Windows";
    if (state == DEVICE_STATE_UNPLUGGED)
        return L"Disconnected";
    return L"Not connected";
}
HRESULT audioPolicyAvailable() {
    ComPtr<IPolicyConfig> policy;
    return CoCreateInstance(PolicyClass, nullptr, CLSCTX_INPROC_SERVER, PolicyInterface,
                            reinterpret_cast<void **>(policy.put()));
}
void setDefaultAudio(const std::wstring &id, EDataFlow flow, bool calls) {
    if (id.empty())
        throw std::runtime_error("Choose an audio device first.");
    ComPtr<IMMDeviceEnumerator> e;
    enumerator(e);
    ComPtr<IMMDevice> d;
    check(e->GetDevice(id.c_str(), d.put()), L"Find audio device");
    DWORD state{};
    check(d->GetState(&state), L"Read audio device state");
    if (state != DEVICE_STATE_ACTIVE)
        throw std::runtime_error("This device is disconnected or disabled in Windows.");
    ComPtr<IMMEndpoint> endpoint;
    check(d->QueryInterface(__uuidof(IMMEndpoint), reinterpret_cast<void **>(endpoint.put())),
          L"Read audio device type");
    EDataFlow actual;
    check(endpoint->GetDataFlow(&actual), L"Read audio device type");
    if (flow != actual)
        throw std::runtime_error("This device is the wrong type of audio endpoint.");
    ComPtr<IPolicyConfig> policy;
    check(CoCreateInstance(PolicyClass, nullptr, CLSCTX_INPROC_SERVER, PolicyInterface,
                           reinterpret_cast<void **>(policy.put())),
          L"Windows audio switching is unavailable on this version of Windows");
    const ERole roles[] = {eConsole, eMultimedia, eCommunications};
    std::vector<std::wstring> before;
    int count = calls ? 3 : 2;
    for (int i = 0; i < count; ++i)
        before.push_back(defaultAudio(flow, roles[i]));
    try {
        for (int i = 0; i < count; ++i)
            check(policy->SetDefaultEndpoint(id.c_str(), roles[i]), L"Set default audio device");
        for (int i = 0; i < count; ++i)
            if (defaultAudio(flow, roles[i]) != id)
                throw std::runtime_error("Windows did not keep the requested default audio device.");
    } catch (...) {
        for (int i = 0; i < count; ++i)
            if (!before[i].empty())
                policy->SetDefaultEndpoint(before[i].c_str(), roles[i]);
        throw;
    }
}
} // namespace io
