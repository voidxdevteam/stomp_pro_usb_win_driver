// SPDX-FileCopyrightText: 2026 Sonulab
// SPDX-License-Identifier: GPL-3.0-only

#include "SonulabIds.hpp"

#include <windows.h>
#include <objbase.h>

#include <iasiodrv.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>

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

class WaveRecorder {
public:
    static constexpr std::size_t kMaximumFrames = 512;
    static constexpr std::size_t kQueueBlocks = 2048;

    bool start(const std::filesystem::path& path) {
        if (gBlockFrames <= 0 || gBlockFrames > static_cast<long>(kMaximumFrames)) {
            return false;
        }
        if (path.has_parent_path()) {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) {
                return false;
            }
        }
        file_.open(path, std::ios::binary | std::ios::trunc);
        if (!file_) {
            return false;
        }
        blocks_ = std::make_unique<Block[]>(kQueueBlocks);
        outputPath_ = path;
        writeHeader(0);
        if (!file_) {
            return false;
        }
        writer_ = std::thread([this] { writerLoop(); });
        return true;
    }

    void enqueue(const std::int32_t* left, const std::int32_t* right, long frames) noexcept {
        const auto write = writeSequence_.load(std::memory_order_relaxed);
        const auto read = readSequence_.load(std::memory_order_acquire);
        if (write - read >= kQueueBlocks || frames <= 0 ||
            frames > static_cast<long>(kMaximumFrames)) {
            droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        auto& block = blocks_[write % kQueueBlocks];
        block.frames = static_cast<std::uint32_t>(frames);
        for (long frame = 0; frame < frames; ++frame) {
            block.samples[static_cast<std::size_t>(frame) * 2] = left[frame];
            block.samples[static_cast<std::size_t>(frame) * 2 + 1] = right[frame];
        }
        writeSequence_.store(write + 1, std::memory_order_release);
    }

    bool stop() {
        stopping_.store(true, std::memory_order_release);
        if (writer_.joinable()) {
            writer_.join();
        }
        if (file_) {
            file_.seekp(0, std::ios::beg);
            writeHeader(framesWritten_);
            file_.flush();
        }
        const bool ok = file_.good() && !writeFailed_.load(std::memory_order_relaxed);
        file_.close();
        return ok;
    }

    std::uint64_t framesWritten() const noexcept { return framesWritten_; }
    std::uint64_t droppedBlocks() const noexcept {
        return droppedBlocks_.load(std::memory_order_relaxed);
    }
    const std::filesystem::path& outputPath() const noexcept { return outputPath_; }

private:
#pragma pack(push, 1)
    struct WaveHeader {
        char riff[4]{'R', 'I', 'F', 'F'};
        std::uint32_t riffSize{};
        char wave[4]{'W', 'A', 'V', 'E'};
        char fmt[4]{'f', 'm', 't', ' '};
        std::uint32_t fmtSize{16};
        std::uint16_t format{1};
        std::uint16_t channels{2};
        std::uint32_t sampleRate{48000};
        std::uint32_t byteRate{48000 * 2 * sizeof(std::int32_t)};
        std::uint16_t blockAlign{2 * sizeof(std::int32_t)};
        std::uint16_t bitsPerSample{32};
        char data[4]{'d', 'a', 't', 'a'};
        std::uint32_t dataSize{};
    };
#pragma pack(pop)

    struct Block {
        std::uint32_t frames{};
        std::array<std::int32_t, kMaximumFrames * 2> samples{};
    };

    void writeHeader(std::uint64_t frames) {
        const auto bytes = frames * 2 * sizeof(std::int32_t);
        if (bytes > std::numeric_limits<std::uint32_t>::max() - 36) {
            writeFailed_.store(true, std::memory_order_relaxed);
            return;
        }
        WaveHeader header{};
        header.dataSize = static_cast<std::uint32_t>(bytes);
        header.riffSize = 36 + header.dataSize;
        file_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }

    void writerLoop() {
        for (;;) {
            const auto read = readSequence_.load(std::memory_order_relaxed);
            const auto write = writeSequence_.load(std::memory_order_acquire);
            if (read == write) {
                if (stopping_.load(std::memory_order_acquire)) {
                    break;
                }
                Sleep(1);
                continue;
            }

            const auto& block = blocks_[read % kQueueBlocks];
            const auto bytes = static_cast<std::streamsize>(
                static_cast<std::size_t>(block.frames) * 2 * sizeof(std::int32_t));
            file_.write(reinterpret_cast<const char*>(block.samples.data()), bytes);
            if (!file_) {
                writeFailed_.store(true, std::memory_order_relaxed);
            }
            framesWritten_ += block.frames;
            readSequence_.store(read + 1, std::memory_order_release);
        }
    }

    std::unique_ptr<Block[]> blocks_;
    std::ofstream file_;
    std::filesystem::path outputPath_;
    std::thread writer_;
    std::atomic<std::uint64_t> writeSequence_{0};
    std::atomic<std::uint64_t> readSequence_{0};
    std::atomic<std::uint64_t> droppedBlocks_{0};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> writeFailed_{false};
    std::uint64_t framesWritten_{0};
};

std::unique_ptr<WaveRecorder> gRecorder;

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
    if (gRecorder) {
        gRecorder->enqueue(inputLeft, inputRight, gBlockFrames);
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
using GetDiagnosticsFunction = void(__stdcall*)(std::uint64_t*, std::uint32_t);
}

int wmain(int argc, wchar_t** argv) {
    const std::filesystem::path executable = argc > 0 ? argv[0] : L".";
    const bool useRegistration = argc > 1 && std::wstring(argv[1]) == L"--registered";
    const std::filesystem::path dllPath = argc > 1 && !useRegistration
        ? std::filesystem::path(argv[1])
        : executable.parent_path() / L"SonulabStompProDriver.dll";
    const int seconds = argc > 2 ? std::max(1, _wtoi(argv[2])) : 10;
    if (argc > 3) {
        gBlockFrames = _wtoi(argv[3]);
    }
    const std::filesystem::path recordingPath = argc > 4
        ? std::filesystem::path(argv[4])
        : std::filesystem::path{};
    if (gBlockFrames <= 0 || gBlockFrames > static_cast<long>(WaveRecorder::kMaximumFrames)) {
        std::cerr << "Buffer size must be between 1 and " << WaveRecorder::kMaximumFrames << " frames\n";
        return 1;
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
        std::cerr << "Unable to instantiate Sonulab StompPRO USB Driver (HRESULT 0x"
                  << std::hex << hr << ")\n";
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
    if (asioError == ASE_OK && !recordingPath.empty()) {
        gRecorder = std::make_unique<WaveRecorder>();
        if (!gRecorder->start(recordingPath)) {
            std::wcerr << L"Unable to create recording " << recordingPath << L'\n';
            gRecorder.reset();
            driver->disposeBuffers();
            driver->Release();
            if (module) FreeLibrary(module);
            if (SUCCEEDED(coHr)) CoUninitialize();
            return 5;
        }
    }
    if (asioError == ASE_OK) {
        asioError = driver->start();
    }
    if (asioError != ASE_OK) {
        char error[124]{};
        driver->getErrorMessage(error);
        std::cerr << "Stream start failed: " << error << " (ASIO " << asioError << ")\n";
        if (gRecorder) {
            gRecorder->stop();
            gRecorder.reset();
        }
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
    std::uint64_t recordedFrames = 0;
    std::uint64_t droppedRecordBlocks = 0;
    bool recordingOk = true;
    if (gRecorder) {
        recordingOk = gRecorder->stop();
        recordedFrames = gRecorder->framesWritten();
        droppedRecordBlocks = gRecorder->droppedBlocks();
        std::wcout << L"recording=" << gRecorder->outputPath() << L'\n';
        gRecorder.reset();
    }
    driver->disposeBuffers();
    driver->Release();
    std::array<std::uint64_t, 12> diagnostics{};
    if (module) {
        const auto getDiagnostics = reinterpret_cast<GetDiagnosticsFunction>(
            GetProcAddress(module, "SonulabGetDiagnostics"));
        if (getDiagnostics) {
            getDiagnostics(diagnostics.data(), static_cast<std::uint32_t>(diagnostics.size()));
        }
    }
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
              << " recorded_frames=" << recordedFrames
              << " record_dropped_blocks=" << droppedRecordBlocks
              << " recording_ok=" << (recordingOk ? "yes" : "no")
              << " capture_packets=" << diagnostics[0]
              << " capture_frames=" << diagnostics[1]
              << " capture_raw_zero_frames=" << diagnostics[2]
              << " capture_silent_frames=" << diagnostics[3]
              << " capture_discontinuities=" << diagnostics[4]
              << " input_underflow_frames=" << diagnostics[5]
              << " render_events=" << diagnostics[6]
              << " render_timeouts=" << diagnostics[7]
              << " render_underflow_frames=" << diagnostics[8]
              << " capture_level_min=" << diagnostics[9]
              << " capture_level_max=" << diagnostics[10]
              << " rate_ppm_min=" << static_cast<std::int32_t>(diagnostics[11] & 0xffffffffU)
              << " rate_ppm_max=" << static_cast<std::int32_t>(diagnostics[11] >> 32)
              << " dll_unload=" << (useRegistration ? "registry" : (unloadResult == S_OK ? "yes" : "no")) << '\n';
    const bool callbackRateOk = callbackCount >= expectedCallbacks * 95 / 100;
    const bool recordingComplete = recordingPath.empty() ||
        (recordingOk && droppedRecordBlocks == 0 &&
         recordedFrames >= callbackCount * static_cast<std::uint64_t>(gBlockFrames) * 99 / 100);
    return callbackRateOk && gLateCallbacks.load() == 0 && gOverloads.load() == 0 &&
        recordingComplete ? 0 : 4;
}
