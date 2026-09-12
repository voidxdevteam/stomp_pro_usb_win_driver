# Sonulab StompPRO USB Driver: development and distribution

The project is a GPL-3.0-only Windows ASIO driver for the Sonulab StompPRO USB
Audio 2.0 interface. It uses the native Microsoft USB Audio 2.0 stack through
exclusive, event-driven WASAPI; it does not install a kernel-mode USB driver.

## Driver characteristics

- 2 capture channels and 2 playback channels;
- 48 kHz, signed 32-bit little-endian PCM;
- 128, 256 or 512-sample ASIO host buffers;
- x64 and x86 ASIO host support on 64-bit Windows;
- endpoint selection through the USB PnP hierarchy using `VID_1D6B&PID_0104`;
- adaptive capture clock synchronization;
- MMCSS `Pro Audio` scheduling for the callback thread.

## License

Sonulab's source code is licensed under `GPL-3.0-only`; see [LICENSE](LICENSE).
The ASIO SDK 2.3.4 interface files under `third_party/asio` are copyright
Steinberg Media Technologies GmbH and are used under their GPLv3 option. Their
original notices and license are retained. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

ASIO is a trademark and software of Steinberg Media Technologies GmbH. The name
is used only to describe compatibility. It is not part of the product name, and
no ASIO logo is distributed.

## Build

Requirements are Visual Studio 2022 with the Desktop C++ workload and CMake 3.24
or newer. The required ASIO SDK interface files are already included.

```powershell
.\tools\build.ps1
```

This builds both architectures under `build\x64\Release` and
`build\x86\Release`. Pass `-Architecture x64` or `-Architecture Win32` to build
only one target. Both targets use the static Microsoft C++ runtime.

## Direct driver test

Connect StompPRO, connect Out 1 to In 1 for loopback measurement, and run each
matching executable and DLL pair:

```powershell
.\build\x64\Release\sonulab_asio_smoke.exe .\build\x64\Release\SonulabStompProDriver.dll 20 256
.\build\x86\Release\sonulab_asio_smoke.exe .\build\x86\Release\SonulabStompProDriver.dll 20 256
```

The test opens capture and playback, outputs a 200 Hz tone and reports callback
timing, buffer levels, clock correction and endpoint discontinuities.

To record and validate the full waveform:

```powershell
.\build\x64\Release\sonulab_asio_smoke.exe .\build\x64\Release\SonulabStompProDriver.dll 3600 256 .\test-results\loopback-60min.wav
python .\tools\analyze-sine-recording.py .\test-results\loopback-60min.wav
```

The final `record_dropped_blocks` and `anomaly_windows` values must both be zero.

## Registration test

The development registration scripts install and remove both architectures and
require administrator privileges:

```powershell
.\tools\register-driver.ps1
.\build\x64\Release\sonulab_asio_smoke.exe --registered 20 256
.\build\x86\Release\sonulab_asio_smoke.exe --registered 20 256
.\tools\unregister-driver.ps1
```

## Release package

Install Inno Setup 6, commit all release changes, then run:

```powershell
.\tools\package-release.ps1
```

The script builds both architectures and creates the unsigned installer, binary
ZIP, corresponding-source ZIP and SHA-256 manifest under `dist`.

Every distributed binary must be accompanied by access to the complete source
for that exact release, including the vendored ASIO interface files and build and
installation scripts. Publish the source archive or matching source tag together
with the installer at no additional charge.
