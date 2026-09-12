# Sonulab StompPRO USB Driver

Open-source Windows audio driver for the Sonulab StompPRO USB Audio 2.0
interface. The project is licensed under GNU GPL version 3 only.

The driver exposes one full-duplex, ASIO-compatible device and uses the native
Windows USB Audio 2.0 stack through exclusive, event-driven WASAPI:

- 2 capture channels and 2 playback channels;
- 48 kHz, signed 32-bit little-endian PCM;
- 128, 256, or 512-sample host buffers;
- one hardware clock;
- endpoint selection through the USB PnP hierarchy using `VID_1D6B&PID_0104`;
- adaptive capture clock synchronization for stable full-duplex operation;
- MMCSS `Pro Audio` scheduling for the callback thread.

The Windows mixer and sample-rate conversion are bypassed. USB transport is
provided by Microsoft's `usbaudio2.sys`; this project does not install a
kernel-mode driver.

## License

Sonulab's source code is licensed under `GPL-3.0-only`; see [LICENSE](LICENSE).
The ASIO SDK interface files under `third_party/asio` are copyright Steinberg
Media Technologies GmbH and are used under their GPLv3 option. Their original
notices and license are retained.

ASIO is a trademark and software of Steinberg Media Technologies GmbH. The
trademark is used only to describe compatibility. It is not part of this
product's name, and no ASIO logo is distributed.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the exact SDK version,
official source URL, archive hash, and vendored files.

## Build

Requirements: Windows 10 or 11, Visual Studio 2022 with the Desktop C++
workload, and CMake 3.24 or newer.

The required ASIO SDK 2.3.4 interface files are included, so the repository is
self-contained:

```powershell
.\tools\build.ps1
```

The x64 and x86 DLLs and smoke-test executables are written to
`build\x64\Release` and `build\x86\Release`.
Pass `-Architecture x64` or `-Architecture Win32` to build only one target.

## Test without installation

Connect the StompPRO, then run:

```powershell
.\build\x64\Release\sonulab_asio_smoke.exe .\build\x64\Release\SonulabStompProDriver.dll 20 256
```

The smoke test opens both directions, outputs a 200 Hz tone, reads both capture
channels, and reports callback timing and discontinuity notifications. With
Out 1 connected to In 1, `capture_peak` should be nonzero.

Run the equivalent x86 executable to validate the DLL used by 32-bit hosts:

```powershell
.\build\x86\Release\sonulab_asio_smoke.exe .\build\x86\Release\SonulabStompProDriver.dll 20 256
```

To retain and validate the complete captured waveform, pass a WAV path as the
last argument and analyze it after the run:

```powershell
.\build\x64\Release\sonulab_asio_smoke.exe .\build\x64\Release\SonulabStompProDriver.dll 3600 256 .\test-results\loopback-60min.wav
python .\tools\analyze-sine-recording.py .\test-results\loopback-60min.wav
```

Recording uses a preallocated single-producer/single-consumer queue. The audio
callback only copies capture samples into that queue; a separate thread writes
the WAV file. The final `record_dropped_blocks` value must be zero. The analyzer
checks every window for silence, clipping, amplitude changes, residual error,
sample steps, phase jumps, and DC offset.

## Install for audio hosts

Close audio applications and run the unsigned installer:

`Sonulab-StompPRO-USB-Driver-0.3.0-Setup.exe`

Windows requests administrator approval. The installer copies and registers the
driver, includes the GPL license and source reference, and adds a standard entry
to Windows Installed apps for removal. Windows may display an unknown-publisher
warning because the installer is intentionally unsigned.

For development builds, the PowerShell registration script remains available:

```powershell
.\tools\register-driver.ps1
```

The driver appears as `Sonulab StompPRO USB Driver` in compatible audio hosts.
The installer supports both 64-bit and 32-bit ASIO hosts on 64-bit Windows.
Windows 10 version 1809 or newer and Windows 11 are supported; a 64-bit edition
of Windows is required.

Test the same COM registration path used by an audio host with:

```powershell
.\build\x64\Release\sonulab_asio_smoke.exe --registered 20 256
.\build\x86\Release\sonulab_asio_smoke.exe --registered 20 256
```

Remove the registration with:

```powershell
.\tools\unregister-driver.ps1
```

## Build the installer

Install Inno Setup 6, then run:

```powershell
.\tools\build-installer.ps1
```

The unsigned setup executable is written to `dist`. The full release script
builds the DLL, binary ZIP, source ZIP, installer, and SHA-256 manifest together.
## Distributing binaries

Every distributed DLL must be accompanied by access to the complete
corresponding source for that exact release, including this repository, the
vendored ASIO SDK interface files, and the build and installation scripts.
A practical release should publish the source tag and binaries together.
