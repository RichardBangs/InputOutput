# Final testing on your hardware

## Set up your menu

1. Put `InputOutput.exe` in the folder where you want to keep it, then open it.
2. With the two monitors configured as desired, capture **2 monitors** on the Displays page.
3. Use Windows Display Settings to enable the TV and disable both monitors. Capture **TV** in InputOutput. Return to your monitor setup through Windows for the first test.
4. In **Audio output**, check your Audioengine speakers, Arctis headphones, and the LG TV's HDMI audio endpoint. Name them **Speakers**, **Headphones**, and **TV speakers**. Use the TV endpoint that becomes **Available** when the TV is enabled.
5. In **Microphone**, check the microphones you want to select. Rename them if useful.
6. Choose any desired fallback output/microphone and check the startup option under **General**.

## Try the combinations

- **2 monitors + Speakers**: both monitors enabled, TV disabled, audio from the speakers.
- **2 monitors + Headphones**: the same screens, audio from the headset.
- **TV + TV speakers**: only the TV enabled, HDMI sound available and selected.
- **TV + Headphones**: only the TV enabled, audio from the headset.
- Switch microphones independently, checking the default input in Windows Sound Settings or your usual app configured to use the Windows default.

Switch to each saved display preset, including a newly captured or updated one, and wait at least 35 seconds. The selected layout should remain active with no confirmation prompt or timed revert. While switching, try clicking elsewhere, pressing Escape, and using **Minimise**: each should dismiss the switcher without undoing the change. Reopen the tray to verify the current layout is selected.

## Check the edge cases

- Switch from the TV to the monitors while headphones are selected: headphones should stay selected.
- Switch away from the TV while TV speakers are selected: the configured available fallback should be used, or Windows chooses an output if no fallback is configured.
- Disconnect a configured audio device: its saved entry should remain visible but disabled. Reconnect it and check that it becomes available again.
- Change the default device in Windows: the app should show the actual current device when reopened.
- Close Settings: the app should remain in the tray. Reopen Settings and verify names, selected devices, order, and fallback settings survived.
- Exit and reopen the app. Then sign out and back in when convenient to check startup.
- Open and navigate the tray by keyboard. Check the panel on each screen, especially if Windows scaling differs between them.

Some media and voice apps pin a specific device instead of following the Windows default. Set those apps to use the default device when testing routing.

## If something fails

Open **Settings → General → Open data folder** and retain `InputOutput.log`. Note the preset/device, what happened, and whether the TV was enabled and connected. Capture the layout again after changing GPU ports or drivers. The app keeps the previous configuration backup and an unfinished recovery file when recovery cannot complete.

## What was checked before delivery

- Native C++ build and standard-Windows-DLL dependency inspection.
- Automated persistence, corruption handling, Unicode, endpoint availability, display identity/remapping, clone grouping, mode comparison, and watchdog decision tests.
- Separate watchdog process start, ready acknowledgement, and cancellation.
- Read-only enumeration of your displays, speakers/headphones, and microphones; validation of the current two-monitor layout with Windows.
- Native settings-window inspection, capture/rename/save, device-menu configuration, navigation, and idle-resource sampling.

Actual display transitions, audible routing, microphone routing, HDMI reconnection, and sign-in startup are the remaining hardware acceptance tests above.
