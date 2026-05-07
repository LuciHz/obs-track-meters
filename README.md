# Track Meters

Per-track audio meters dock for OBS Studio (Windows).

Displays post-mix, pre-encoder dBFS levels for all 6 audio tracks with peak hold, configurable target lines, and clipping warnings.

## Features

- 6 simultaneous track meters with logarithmic dBFS scale (-60 to 0 dB)
- Peak hold with 1-second hold time and slow decay
- Colour gradient: dark green → bright green → yellow → orange → red
- Target lines at -12 dB (Min) and -3 dB (Max)
- Per-track visibility toggles via Settings dialog
- Batched clipping warnings with 10-second cooldown
- Dock toggleable via View > Docks menu

## Requirements

- OBS Studio 30.0 or newer (Windows 64-bit)
- Tested on OBS 32.1.2

## Installation

1. Download the latest release ZIP from the [Releases page](https://github.com/LuciHz/obs-track-meters/releases).
2. Close OBS Studio.
3. Extract the ZIP into your OBS install directory, typically `C:\Program Files\obs-studio\`. The `obs-plugins\` and `data\` folders will merge with the existing OBS folders.
4. Launch OBS. The "Track Meters" dock will appear. If not visible, enable it via View > Docks > Track Meters.

If the plugin fails to load, install the [Microsoft Visual C++ Redistributable 2015–2022 (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

## Building from source

Requires Visual Studio 2022, CMake, Git, and Windows 11 SDK.

```powershell
cmake --preset windows-x64
cmake --build build_x64 --config Release
```

The compiled DLL will be at `build_x64\Release\obs-audio-meters.dll`.

## License

GPL-2.0 — see [LICENSE](LICENSE).

## Acknowledgements

Built on the [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) scaffolding from the OBS Project.