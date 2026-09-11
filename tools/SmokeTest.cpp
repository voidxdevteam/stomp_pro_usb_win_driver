#include "SonulabIds.hpp"

#include <windows.h>
#include <objbase.h>

#include <iasiodrv.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace {
constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::array<ASIOBufferInfo, 4> gBuffers{};
long gBlockFrames = 256;
double gPhase = 0.0;
std::atomic<std::uint64_t> gCallbacks{0};
std::atomic<std::uint64_t> gLateCallbacks{0};
std::atomic<std::uint64_t> gLastCallbackNs{0};
std::atomic<std::uint64_t> gMaximumGapNs{0};
std::atomic<std::int32_t> gCapturePeak{0};
std::atomic<std::uint64_t> gOverloads{0};

std::uint64_t qpcNanoseconds() {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<std::uint64_t>(
        static_cast<long double>(counter.QuadPart) * 1'000'000'000.0L /
        static_cast<long double>(frequency.QuadPart));
}

void updateMaximum(std::atomic<std::uint64_t>& target, std::uint64_t value) {
    auto previous = target.load(std::memory_order_relaxed);
    while (value > previous &&
           !target.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {
    }
}

void updatePeak(std::int32_t value) {
    const std::int32_t magnitude = value == std::numeric_limits<std::int32_t>::min()
        ? std::numeric_limits<std::int32_t>::max()
        : std::abs(value);
    auto previous = gCapturePeak.load(std::memory_order_relaxed);
    while (magnitude > previous &&
           !gCapturePeak.compare_exchange_weak(previous, magnitude, std::memory_order_relaxed)) {
    }
}

void processBuffer(long index) {
    const auto now = qpcNanoseconds();
    const auto previous = gLastCallbackNs.exchange(now, std::memory_order_relaxed);
    if (previous != 0) {
        const auto gap = now - previous;
        updateMaximum(gMaximumGapNs, gap);
        const auto expected = static_cast<std::uint64_t>(
            static_cast<double>(gBlockFrames) * 1'000'000'000.0 / kSampleRate);
        if (gap > expected * 3) {
            gLateCallbacks.fetch_add(1, std::memory_order_relaxed);
        }
    }

    auto* inputLeft = static_cast<const std::int32_t*>(gBuffers[0].buffers[index]);
    auto* inputRight = static_cast<const std::int32_t*>(gBuffers[1].buffers[index]);
    auto* outputLeft = static_cast<std::int32_t*>(gBuffers[2].buffers[index]);
    auto* outputRight = static_cast<std::int32_t*>(gBuffers[3].buffers[index]);

    for (long frame = 0; frame < gBlockFrames; ++frame) {
        updatePeak(inputLeft[frame]);
        updatePeak(inputRight[frame]);
        const auto value = static_cast<std::int32_t>(
            std::sin(gPhase) * (static_cast<double>(std::numeric_limits<std::int32_t>::max()) * 0.1));
        outputLeft[frame] = value;
        outputRight[frame] = value;
        gPhase += 2.0 * kPi * 200.0 / kSampleRate;
        if (gPhase >= 2.0 * kPi) {
            gPhase -= 2.0 * kPi;
        }
    }
    gCallbacks.fetch_add(1, std::memory_order_relaxed);
}

void bufferSwitch(long index, ASIOBool) {
    processBuffer(index);
}

void sampleRateDidChange(ASIOSampleRate) {
}

long asioMessage(long selector, long value, void*, double*) {
    if (selector == kAsioSelectorSupported) {
        return value == kAsioSupportsTimeInfo || value == kAsioOverload;
    }
    if (selector == kAsioEngineVersion) {
        return 2;
    }
    if (selector == kAsioSupportsTimeInfo) {
        return 1;
    }
    if (selector == kAsioOverload) {
        gOverloads.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    return 0;
}

ASIOTime* bufferSwitchTimeInfo(ASIOTime* timeInfo, long index, ASIOBool) {
    processBuffer(index);
    return timeInfo;
}

using DllGetClassObjectFunction = HRESULT(__stdcall*)(REFCLSID, REFIID, void**);
}

int wmain(int argc, wchar_t** argv) {
    const std::filesystem::path executable = argc > 0 ? argv[0] : L".";
    const bool useRegistration = argc > 1 && std::wstring(argv[1]) == L"--registered";
    const std::filesystem::path dllPath = argc > 1 && !useRegistration
        ? std::filesystem::path(argv[1])
        : executable.parent_path() / L"SonulabASIO.dll";
    const int seconds = argc > 2 ? std::max(1, _wtoi(argv[2])) : 10;
    if (argc > 3) {
        gBlockFrames = _wtoi(argv[3]);
    }

    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HMODULE module = nullptr;
    IASIO* driver = nullptr;
    HRESULT hr = E_FAIL;
    if (useRegistration) {
        hr = CoCreateInstance(
            CLSID_SonulabASIO,
            nullptr,
            CLSCTX_INPROC_SERVER,
            CLSID_SonulabASIO,
            reinterpret_cast<void**>(&driver));
    } else {
        module = LoadLibraryW(dllPath.c_str());
        if (!module) {
            std::wcerr << L"Unable to load " << dllPath << L" (Win32 " << GetLastError() << L")\n";
            return 1;
        }

        const auto getClassObject = reinterpret_cast<DllGetClassObjectFunction>(
            GetProcAddress(module, "DllGetClassObject"));
        if (!getClassObject) {
            std::cerr << "DllGetClassObject export is missing\n";
            FreeLibrary(module);
            return 1;
        }

        IClassFactory* factory = nullptr;
        hr = getClassObject(CLSID_SonulabASIO, IID_IClassFactory, reinterpret_cast<void**>(&factory));
        if (SUCCEEDED(hr)) {
            hr = factory->CreateInstance(nullptr, CLSID_SonulabASIO, reinterpret_cast<void**>(&driver));
            factory->Release();
        }
    }
    if (FAILED(hr) || !driver) {
        std::cerr << "Unable to instantiate Sonulab ASIO (HRESULT 0x" << std::hex << hr << ")\n";
        if (module) FreeLibrary(module);
        return 1;
    }

    if (!driver->init(nullptr)) {
        char error[124]{};
        driver->getErrorMessage(error);
        std::cerr << "Driver initialization failed: " << error << '\n';
        driver->Release();
        if (module) FreeLibrary(module);
        return 2;
    }

    gBuffers[0] = {ASIOTrue, 0, {nullptr, nullptr}};
    gBuffers[1] = {ASIOTrue, 1, {nullptr, nullptr}};
    gBuffers[2] = {ASIOFalse, 0, {nullptr, nullptr}};
    gBuffers[3] = {ASIOFalse, 1, {nullptr, nullptr}};
    ASIOCallbacks callbacks{bufferSwitch, sampleRateDidChange, asioMessage, bufferSwitchTimeInfo};
    ASIOError asioError = driver->createBuffers(
        gBuffers.data(), static_cast<long>(gBuffers.size()), gBlockFrames, &callbacks);
    if (asioError == ASE_OK) {
        asioError = driver->start();
    }
    if (asioError != ASE_OK) {
        char error[124]{};
        driver->getErrorMessage(error);
        std::cerr << "Stream start failed: " << error << " (ASIO " << asioError << ")\n";
        driver->disposeBuffers();
        driver->Release();
        if (module) FreeLibrary(module);
        return 3;
    }

    long inputLatency = 0;
    long outputLatency = 0;
    driver->getLatencies(&inputLatency, &outputLatency);

    std::cout << "Running 200 Hz full-duplex smoke test for " << seconds
              << " seconds at 48 kHz / " << gBlockFrames
              << " samples (latency in/out " << inputLatency << '/' << outputLatency << ")...\n";
    Sleep(static_cast<DWORD>(seconds * 1000));

    driver->stop();
    driver->disposeBuffers();
    driver->Release();
    HRESULT unloadResult = S_OK;
    if (module) {
        unloadResult = reinterpret_cast<HRESULT(__stdcall*)()>(
            GetProcAddress(module, "DllCanUnloadNow"))();
        FreeLibrary(module);
    }
    if (SUCCEEDED(coHr)) {
        CoUninitialize();
    }

    const auto callbackCount = gCallbacks.load();
    const auto expectedCallbacks = static_cast<std::uint64_t>(
        static_cast<double>(seconds) * kSampleRate / static_cast<double>(gBlockFrames));
    std::cout << "callbacks=" << callbackCount
              << " late_callbacks=" << gLateCallbacks.load()
              << " max_gap_ms=" << static_cast<double>(gMaximumGapNs.load()) / 1'000'000.0
              << " overloads=" << gOverloads.load()
              << " capture_peak=" << gCapturePeak.load()
              << " dll_unload=" << (useRegistration ? "registry" : (unloadResult == S_OK ? "yes" : "no")) << '\n';
    const bool callbackRateOk = callbackCount >= expectedCallbacks * 95 / 100;
    return callbackRateOk && gLateCallbacks.load() == 0 && gOverloads.load() == 0 ? 0 : 4;
}
