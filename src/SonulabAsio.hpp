// SPDX-FileCopyrightText: 2026 Sonulab
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "WasapiBackend.hpp"

#include <iasiodrv.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

void sonulabObjectCreated() noexcept;
void sonulabObjectDestroyed() noexcept;

class SonulabAsio final : public IASIO {
public:
    SonulabAsio();
    ~SonulabAsio();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    ASIOBool init(void* sysHandle) override;
    void getDriverName(char* name) override;
    long getDriverVersion() override;
    void getErrorMessage(char* string) override;
    ASIOError start() override;
    ASIOError stop() override;
    ASIOError getChannels(long* numInputChannels, long* numOutputChannels) override;
    ASIOError getLatencies(long* inputLatency, long* outputLatency) override;
    ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) override;
    ASIOError canSampleRate(ASIOSampleRate sampleRate) override;
    ASIOError getSampleRate(ASIOSampleRate* sampleRate) override;
    ASIOError setSampleRate(ASIOSampleRate sampleRate) override;
    ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) override;
    ASIOError setClockSource(long reference) override;
    ASIOError getSamplePosition(ASIOSamples* samplePosition, ASIOTimeStamp* timestamp) override;
    ASIOError getChannelInfo(ASIOChannelInfo* info) override;
    ASIOError createBuffers(
        ASIOBufferInfo* bufferInfos,
        long numChannels,
        long bufferSize,
        ASIOCallbacks* callbacks) override;
    ASIOError disposeBuffers() override;
    ASIOError controlPanel() override;
    ASIOError future(long selector, void* option) override;
    ASIOError outputReady() override;

private:
    static constexpr long kChannels = 2;
    static constexpr double kSampleRate = 48000.0;
    static constexpr std::array<long, 3> kBufferSizes{128, 256, 512};

    void processCycle(
        const std::int32_t* inputInterleaved,
        std::int32_t* outputInterleaved,
        std::uint32_t frames,
        std::uint64_t samplePosition,
        std::uint64_t timestampNs,
        bool discontinuity) noexcept;
    void setError(std::string message);
    static void setAsio64(ASIOSamples& destination, std::uint64_t value) noexcept;
    static void setAsio64(ASIOTimeStamp& destination, std::uint64_t value) noexcept;
    static void copyAsioString(char* destination, std::size_t capacity, const char* source) noexcept;

    std::atomic<ULONG> references_{1};
    bool initialized_ = false;
    bool running_ = false;
    long blockFrames_ = 256;
    long bufferIndex_ = 0;
    bool timeInfoMode_ = false;
    ASIOCallbacks* callbacks_ = nullptr;
    ASIOTime timeInfo_{};
    std::atomic<std::uint64_t> samplePosition_{0};
    std::atomic<std::uint64_t> timestampNs_{0};
    std::string error_;

    std::array<std::array<std::int32_t*, 2>, kChannels> inputBuffers_{};
    std::array<std::array<std::int32_t*, 2>, kChannels> outputBuffers_{};
    std::vector<std::unique_ptr<std::int32_t[]>> allocations_;
    WasapiBackend backend_;
};
