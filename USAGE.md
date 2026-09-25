# InputOutput

A small Windows 11 tray app for independently switching display presets, audio outputs, and microphones.

## Run

Open `InputOutput.exe`. The first launch opens Settings. The app lives in the notification area; click or right-click its monitor icon to open the switcher. You may need to drag the icon out of Windows' hidden-icons area.

Keep the executable in a permanent folder. Startup is enabled on the first normal launch and can be changed under **Settings → General**. It starts at your Windows sign-in, without an administrator prompt. The app does not automatically apply a display preset at startup.

## Configure

### Displays

1. Arrange and enable the screens you want using Windows Display Settings.
2. In **Settings → Displays**, select **Capture current as new**.
3. Give it a short name, such as **2 monitors**, then click the **Save name** icon beside the name field.
4. Set up the TV in Windows, capture another preset, and name it **TV**.

Presets capture active screens, positions, resolution, refresh rate, rotation, and the primary screen. Screens outside a preset are disabled when it is applied. You can rename, update, reorder, hide, or delete presets. The checkbox beside each preset controls whether it appears in the tray menu.

Saved display presets stay applied once Windows verifies the requested layout, including on their first use. There is no confirmation countdown. A separate recovery process protects the switch while it is in progress; it attempts to restore the previous layout if applying or verifying the change fails, the app exits mid-switch, or the switch stalls. It exits immediately after a successful switch. Windows and HDMI hardware determine how long a physical display change takes.

### Audio output and microphone

Each has its own page in the Settings sidebar. Check the devices you want in the tray menu, give them short menu names, and arrange their order. Settings lists current and disconnected endpoints; stale, unconfigured historical endpoints are hidden. Unavailable saved choices stay visible but cannot be selected.

Optionally choose a fallback device for each category. **Let Windows choose** is the default. A fallback is used only when it is available. Changing the display setup attempts to preserve the selected output and microphone when those devices remain available. HDMI audio becomes selectable once Windows reports it as available.

**General → Also use selected audio devices for calls** makes each output or microphone selection update Windows' default communication device too. Calling apps that follow that Windows setting will use the selected device. Turning this off leaves the communication defaults unchanged when switching devices. Applications with their own explicit device preferences may keep using those devices. InputOutput never opens a microphone stream or records sound.

**General → View on GitHub** opens the [project page](https://github.com/RichardBangs/InputOutput) in your default browser.

## Daily use

Open the tray panel, select a display preset, and select an audio output or microphone if desired. The panel stays open for multiple selections. Click elsewhere, press Escape, or choose **Minimise** to return it to the tray, including while a device switch is running. Dismissing it does not undo a successful display change. Tab, Shift+Tab, Enter, and Space work with the native controls; long menus scroll.

Monitor, speaker, and microphone icons identify the three groups. Hover over an icon to see its label.

Closing Settings leaves the app running. **Exit** in the tray panel closes it.

You can also open the switcher directly with `InputOutput.exe --popup`, for example from a shortcut. It opens the existing instance when the app is already running.

## Data and recovery

Normal settings are stored in `%LOCALAPPDATA%\InputOutput\settings.dat`, with the previous version in `settings.dat.bak`. Writes are atomic and include an integrity check. A bounded local log helps diagnose errors. **Settings → General → Open data folder** opens these files.

If a settings file is damaged or uses an unsupported version, the app retains it and refuses to overwrite it. Exit the app and restore the backup, or move the damaged file aside to start fresh. A retained `display-recovery.dat` is offered for recovery at the next launch.

To remove the app, turn off startup in General, exit, and delete its executable and optional data folder.

## Scope

- Windows 11, x64, standard interactive desktop session.
- One executable, standard Windows DLLs, no downloaded runtime, service, browser engine, or background polling loop.
- Display identification uses monitor device paths and remaps adapter/source/target numbers at switch time. Changing ports, reinstalling drivers, or replacing hardware can require recapturing a preset or reselecting an endpoint.
- HDR, Windows scaling percentage, brightness, monitor power buttons, per-app audio routing, microphone gain/mute, and volume are not captured in this version.
- Windows' default-audio setter uses the undocumented `IPolicyConfig` interface. Its implementation is isolated in `src/audio.cpp`; Windows updates can affect compatibility.
- The initial build is unsigned. Release signing and automatic updates are not part of this version.

## Build

PowerShell:

```powershell
.\build.ps1 -Bootstrap -Test -Package
```

The first build downloads a pinned, SHA-256-verified LLVM-MinGW toolchain into `.tools`. Later builds are offline and use `build.ps1 -Test -Package`. No machine-wide compiler installation is required. The application uses the C++20 standard library and native Windows APIs. Build tools are not shipped with it.

The committed icon is generated by `scripts/make-icon.py`; Python is only needed if regenerating that asset. CMake is also provided for existing Windows C++ development environments, including MSVC; the packaged build is verified with LLVM-MinGW.

Developer checks:

```powershell
# Automated tests; creates only files inside the specified folder.
.\dist\InputOutput.exe --self-test --data-dir "$PWD\test-output\self-test"

# Read-only device discovery and display validation; writes diagnostics.txt.
.\dist\InputOutput.exe --diagnose --data-dir "$PWD\test-output\diagnostics"

# UI navigation smoke check; isolated settings and startup registration disabled.
.\dist\InputOutput.exe --ui-smoke --data-dir "$PWD\test-output\ui-smoke"

# Manual UI development session without startup registration.
.\dist\InputOutput.exe --settings --no-startup --data-dir "$PWD\test-output\manual"
```

See [TESTING.md](TESTING.md) for the hardware acceptance test.
