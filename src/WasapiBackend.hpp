// SPDX-FileCopyrightText: 2026 Sonulab
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class WasapiBackend final {
public:
    using CycleCallback = std::function<void(
        const std::int32_t* inputInterleaved,
        std::int32_t* outputInterleaved,
        std::uint32_t frames,
        std::uint64_t samplePosition,
        std::uint64_t timestampNs,
        bool discontinuity)>;

    WasapiBackend();
    ~WasapiBackend();

    WasapiBackend(const WasapiBackend&) = delete;
    WasapiBackend& operator=(const WasapiBackend&) = delete;

    bool probe(std::string& error);
    bool open(std::uint32_t asioBlockFrames, CycleCallback callback, std::string& error);
    bool start(std::string& error);
    void stop();
    void close();

    [[nodiscard]] std::uint32_t renderEndpointFrames() const noexcept { return renderEndpointFrames_; }
    [[nodiscard]] std::uint32_t captureEndpointFrames() const noexcept { return captureEndpointFrames_; }

private:
    class FrameRing {
    public:
        void reset(std::size_t capacityFrames);
        void clear() noexcept;
        [[nodiscard]] std::size_t sizeFrames() const noexcept { return sizeFrames_; }
        [[nodiscard]] std::size_t freeFrames() const noexcept { return capacityFrames_ - sizeFrames_; }
        void push(const std::int32_t* interleaved, std::size_t frames) noexcept;
        std::size_t pop(std::int32_t* interleaved, std::size_t frames) noexcept;
        std::size_t resamplePop(
            std::int32_t* interleaved,
            std::size_t outputFrames,
            double inputFramesPerOutputFrame,
            double& phase) noexcept;

    private:
        static constexpr std::size_t kChannels = 2;
        std::vector<std::int32_t> data_;
        std::size_t capacityFrames_ = 0;
        std::size_t readFrame_ = 0;
        std::size_t writeFrame_ = 0;
        std::size_t sizeFrames_ = 0;
    };

    bool findEndpoint(EDataFlow flow, Microsoft::WRL::ComPtr<IMMDevice>& device, std::string& error);
    bool initializeEndpoint(
        IMMDevice* device,
        Microsoft::WRL::ComPtr<IAudioClient>& client,
        HANDLE eventHandle,
        std::uint32_t& endpointFrames,
        std::string& error);
    void threadMain();
    void drainCapture(bool& discontinuity) noexcept;
    void renderAvailableFrames(bool discontinuity, bool endpointEvent) noexcept;
    void makeAsioBlock(bool discontinuity) noexcept;
    static std::uint64_t qpcNanoseconds() noexcept;
    static std::string hresultMessage(const char* operation, HRESULT hr);

    bool coInitialized_ = false;
    std::uint32_t asioBlockFrames_ = 0;
    std::uint32_t renderEndpointFrames_ = 0;
    std::uint32_t captureEndpointFrames_ = 0;
    CycleCallback callback_;

    Microsoft::WRL::ComPtr<IMMDevice> renderDevice_;
    Microsoft::WRL::ComPtr<IMMDevice> captureDevice_;
    Microsoft::WRL::ComPtr<IAudioClient> renderClient_;
    Microsoft::WRL::ComPtr<IAudioClient> captureClient_;
    Microsoft::WRL::ComPtr<IAudioRenderClient> renderService_;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureService_;

    HANDLE stopEvent_ = nullptr;
    HANDLE renderEvent_ = nullptr;
    HANDLE captureEvent_ = nullptr;
    std::thread audioThread_;
    std::atomic<bool> running_ = false;

    FrameRing captureRing_;
    FrameRing renderRing_;
    std::vector<std::int32_t> inputBlock_;
    std::vector<std::int32_t> outputBlock_;
    std::vector<std::int32_t> captureScratch_;
    std::uint64_t samplePosition_ = 0;
    bool pendingDiscontinuity_ = false;
    std::size_t captureTargetFrames_ = 0;
    double captureLevelFiltered_ = 0.0;
    double captureResamplePhase_ = 0.0;
};
