# Sonulab StompPRO USB Driver

This driver lets you use StompPRO as a low-latency audio interface with ASIO
applications on Windows. It provides two inputs and two outputs through a
single full-duplex device, so recording and playback are available together.

## System requirements

- Windows 10 version 1809 or newer, or Windows 11;
- a 64-bit edition of Windows;
- a 32-bit or 64-bit ASIO audio application;
- a Sonulab StompPRO connected by USB.

ASIO4ALL and additional USB audio drivers are not required.

## Installation

1. Close your music and audio applications.
2. Connect and switch on StompPRO.
3. Run `Sonulab-StompPRO-USB-Driver-0.3.1-Setup.exe`.
4. Approve the Windows administrator request and complete the installation.
5. Reopen your audio application and select `Sonulab StompPRO USB Driver` as
   its ASIO device.

The current installer is unsigned. Windows may show an unknown-publisher or
SmartScreen warning. Download the installer only from Sonulab or another source
you trust, then use **More info > Run anyway** if Windows displays that prompt.

## Recommended settings

The driver operates at 48 kHz and offers these buffer sizes:

- **256 samples:** recommended starting point;
- **128 samples:** lower latency, with higher CPU demand;
- **512 samples:** greater stability on slower or heavily loaded computers.

StompPRO appears with two input channels and two output channels. Only one
application can use the device in exclusive ASIO mode at a time.

## If the device is unavailable

Try these steps in order:

1. Close every application that may be using StompPRO, including browser tabs,
   media players and other DAWs.
2. Disconnect and reconnect the USB cable.
3. Switch StompPRO off and on.
4. Reopen the audio application and select the Sonulab driver again.
5. If audio still stops or clicks, choose a 256- or 512-sample buffer and avoid
   unpowered USB hubs.

If the driver does not appear in a 32-bit application, reinstall version 0.3.1.
The installer includes separate drivers for both 32-bit and 64-bit applications.

## Removal

Close all audio applications, open **Windows Settings > Apps > Installed apps**,
select **Sonulab StompPRO USB Driver**, and choose **Uninstall**.

## License and source code

The driver is free software licensed under GNU GPL version 3. The release also
includes the complete corresponding source code, license and third-party
notices. ASIO is a trademark and software of Steinberg Media Technologies GmbH.

Developers and distributors can find build, test and release instructions in
[DEVELOPMENT.md](DEVELOPMENT.md).
