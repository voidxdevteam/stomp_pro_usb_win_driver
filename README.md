# Sonulab StompPRO USB Driver

Open-source Windows x64 audio driver for the Sonulab StompPRO USB Audio 2.0
interface. The project is licensed under GNU GPL version 3 only.

The driver exposes one full-duplex, ASIO-compatible device and uses the native
Windows USB Audio 2.0 stack through exclusive, event-driven WASAPI:

- 2 capture channels and 2 playback channels;
- 48 kHz, signed 32-bit little-endian PCM;
- 128, 256, or 512-sample host buffers;
- one hardware clock;
- endpoint selection through the USB PnP hierarchy using `VID_1D6B&PID_0104`;
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

The x64 DLL and smoke-test executable are written to `build\Release`.

## Test without installation

Connect the StompPRO, then run:

```powershell
.\build\Release\sonulab_asio_smoke.exe .\build\Release\SonulabStompProDriver.dll 20 256
```

The smoke test opens both directions, outputs a 200 Hz tone, reads both capture
channels, and reports callback timing and discontinuity notifications. With
Out 1 connected to In 1, `capture_peak` should be nonzero.

## Install for audio hosts

Close audio applications and run the unsigned installer:

`Sonulab-StompPRO-USB-Driver-0.2.0-Setup.exe`

Windows requests administrator approval. The installer copies and registers the
driver, includes the GPL license and source reference, and adds a standard entry
to Windows Installed apps for removal. Windows may display an unknown-publisher
warning because the installer is intentionally unsigned.

For development builds, the PowerShell registration script remains available:

```powershell
.\tools\register-driver.ps1
```

The driver appears as `Sonulab StompPRO USB Driver` in compatible audio hosts.
Only 64-bit hosts are supported by this release.

Test the same COM registration path used by an audio host with:

```powershell
.\build\Release\sonulab_asio_smoke.exe --registered 20 256
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
