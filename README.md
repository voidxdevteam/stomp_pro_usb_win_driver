# Sonulab ASIO

Windows x64 ASIO driver prototype for the Sonulab StompPRO USB Audio 2.0 interface.

The ASIO DLL uses the native Windows USB Audio 2.0 driver through exclusive,
event-driven WASAPI. It exposes one full-duplex ASIO device with:

- 2 capture channels and 2 playback channels;
- 48 kHz, signed 32-bit little-endian PCM;
- 128, 256, or 512-sample ASIO buffers;
- one device clock;
- endpoint selection by USB hardware ID `VID_1D6B&PID_0104`.

The Windows mixer and sample-rate conversion are bypassed. The USB transport
continues to use Microsoft's `usbaudio2.sys`; this project does not install a
kernel-mode driver.

## Build

Requirements: Windows 10/11, Visual Studio 2022 with the Desktop C++ workload,
CMake 3.24 or newer, and the Steinberg ASIO SDK under `third_party/asio`.

From PowerShell:

```powershell
.\tools\build.ps1
```

The x64 DLL and smoke-test executable are written to `build\Release`.

## Test without installation

Connect the StompPRO, then run:

```powershell
.\build\Release\sonulab_asio_smoke.exe .\build\Release\SonulabASIO.dll 20 256
```

The smoke test opens both directions, outputs a 200 Hz tone, reads both capture
channels, and reports callback timing and discontinuity notifications. If Out 1
is connected to In 1, `capture_peak` should be nonzero.

## Register for ASIO hosts

Run the command below; Windows will request administrator approval:

```powershell
.\tools\register-driver.ps1
```

The driver appears as `Sonulab ASIO`. Use `unregister-driver.ps1` to remove it.
Only 64-bit ASIO hosts are supported by this prototype.

To test the same COM registration path used by an ASIO host:

```powershell
.\build\Release\sonulab_asio_smoke.exe --registered 20 256
```

## Distribution

The ASIO SDK in `third_party/asio` is separately licensed by Steinberg. Before
shipping a closed-source commercial build, Sonulab must obtain and comply with
Steinberg's proprietary ASIO SDK license. The SDK also offers GPLv3 terms for a
compatible open-source distribution.
