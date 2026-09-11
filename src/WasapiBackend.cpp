// SPDX-FileCopyrightText: 2026 Sonulab
// SPDX-License-Identifier: GPL-3.0-only

#include "WasapiBackend.hpp"

#include "SonulabIds.hpp"

#include <avrt.h>
#include <cfgmgr32.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <propvarutil.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace {
constexpr std::uint32_t kSampleRate = 48000;
constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kBytesPerSample = 4;
constexpr REFERENCE_TIME kReferenceTimePerSecond = 10'000'000;

std::atomic<std::uint64_t> gCapturePackets{0};
std::atomic<std::uint64_t> gCaptureFrames{0};
std::atomic<std::uint64_t> gCaptureRawZeroFrames{0};
std::atomic<std::uint64_t> gCaptureSilentFrames{0};
std::atomic<std::uint64_t> gCaptureDiscontinuities{0};
std::atomic<std::uint64_t> gInputUnderflowFrames{0};
std::atomic<std::uint64_t> gRenderEvents{0};
std::atomic<std::uint64_t> gRenderTimeouts{0};
std::atomic<std::uint64_t> gRenderUnderflowFrames{0};
std::atomic<std::int64_t> gMinimumCaptureLevel{std::numeric_limits<std::int64_t>::max()};
std::atomic<std::int64_t> gMaximumCaptureLevel{0};
std::atomic<std::int64_t> gMinimumRatePpm{0};
std::atomic<std::int64_t> gMaximumRatePpm{0};

template <typename T>
void updateMinimum(std::atomic<T>& target, T value) noexcept {
    auto previous = target.load(std::memory_order_relaxed);
    while (value < previous &&
           !target.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {
    }
}

template <typename T>
void updateMaximum(std::atomic<T>& target, T value) noexcept {
    auto previous = target.load(std::memory_order_relaxed);
    while (value > previous &&
           !target.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {
    }
}

WAVEFORMATEXTENSIBLE makeFormat() {
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = static_cast<WORD>(kChannels);
    format.Format.nSamplesPerSec = kSampleRate;
    format.Format.wBitsPerSample = static_cast<WORD>(kBytesPerSample * 8);
    format.Format.nBlockAlign = static_cast<WORD>(kChannels * kBytesPerSample);
    format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    return format;
}

std::wstring upper(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towupper(c));
    });
    return value;
}

bool endpointBelongsToSonulab(IMMDevice* endpoint) {
    LPWSTR endpointId = nullptr;
    if (!endpoint || FAILED(endpoint->GetId(&endpointId)) || !endpointId) {
        return false;
    }

    const std::wstring rawEndpointId(endpointId);
    CoTaskMemFree(endpointId);

    DEVINST node = 0;
    std::wstring pnpEndpointId = rawEndpointId;
    CONFIGRET locateResult = CM_Locate_DevNodeW(
        &node,
        pnpEndpointId.data(),
        CM_LOCATE_DEVNODE_NORMAL);
    if (locateResult != CR_SUCCESS && upper(rawEndpointId).rfind(L"SWD\\MMDEVAPI\\", 0) != 0) {
        pnpEndpointId = L"SWD\\MMDEVAPI\\" + rawEndpointId;
        locateResult = CM_Locate_DevNodeW(
            &node,
            pnpEndpointId.data(),
            CM_LOCATE_DEVNODE_NORMAL);
    }
    if (locateResult != CR_SUCCESS) {
        return false;
    }

    const std::wstring wanted = upper(kSonulabUsbHardwareId);
    for (int depth = 0; depth < 8; ++depth) {
        wchar_t deviceId[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(node, deviceId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS &&
            upper(deviceId).find(wanted) != std::wstring::npos) {
            return true;
        }

        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) {
            break;
        }
        node = parent;
    }
    return false;
}
}

WasapiBackend::WasapiBackend() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    coInitialized_ = SUCCEEDED(hr);
}

WasapiBackend::~WasapiBackend() {
    close();
    if (coInitialized_) {
        CoUninitialize();
    }
}

void WasapiBackend::FrameRing::reset(std::size_t capacityFrames) {
    capacityFrames_ = std::max<std::size_t>(capacityFrames, 1);
    data_.assign(capacityFrames_ * kChannels, 0);
    clear();
}

void WasapiBackend::FrameRing::clear() noexcept {
    readFrame_ = 0;
    writeFrame_ = 0;
    sizeFrames_ = 0;
}

void WasapiBackend::FrameRing::push(const std::int32_t* interleaved, std::size_t frames) noexcept {
    if (!interleaved || frames == 0 || capacityFrames_ == 0) {
        return;
    }

    if (frames >= capacityFrames_) {
        interleaved += (frames - capacityFrames_) * kChannels;
        frames = capacityFrames_;
        clear();
    } else if (frames > freeFrames()) {
        const std::size_t discard = frames - freeFrames();
        readFrame_ = (readFrame_ + discard) % capacityFrames_;
        sizeFrames_ -= discard;
    }

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t dst = writeFrame_ * kChannels;
        const std::size_t src = frame * kChannels;
        data_[dst] = interleaved[src];
        data_[dst + 1] = interleaved[src + 1];
        writeFrame_ = (writeFrame_ + 1) % capacityFrames_;
    }
    sizeFrames_ += frames;
}

std::size_t WasapiBackend::FrameRing::pop(std::int32_t* interleaved, std::size_t frames) noexcept {
    if (!interleaved || frames == 0 || capacityFrames_ == 0) {
        return 0;
    }

    const std::size_t count = std::min(frames, sizeFrames_);
    for (std::size_t frame = 0; frame < count; ++frame) {
        const std::size_t src = readFrame_ * kChannels;
        const std::size_t dst = frame * kChannels;
        interleaved[dst] = data_[src];
        interleaved[dst + 1] = data_[src + 1];
        readFrame_ = (readFrame_ + 1) % capacityFrames_;
    }
    sizeFrames_ -= count;
    return count;
}

std::size_t WasapiBackend::FrameRing::resamplePop(
    std::int32_t* interleaved,
    std::size_t outputFrames,
    double inputFramesPerOutputFrame,
    double& phase) noexcept {
    if (!interleaved || outputFrames == 0 || capacityFrames_ == 0 ||
        inputFramesPerOutputFrame <= 0.0 || phase < 0.0 || phase >= 1.0) {
        return 0;
    }

    const double lastPosition =
        phase + static_cast<double>(outputFrames - 1) * inputFramesPerOutputFrame;
    const std::size_t requiredFrames = static_cast<std::size_t>(lastPosition) + 2;
    if (requiredFrames > sizeFrames_) {
        return 0;
    }

    for (std::size_t frame = 0; frame < outputFrames; ++frame) {
        const double position = phase + static_cast<double>(frame) * inputFramesPerOutputFrame;
        const std::size_t firstOffset = static_cast<std::size_t>(position);
        const std::size_t secondOffset = firstOffset + 1;
        const double fraction = position - static_cast<double>(firstOffset);
        const std::size_t firstFrame = (readFrame_ + firstOffset) % capacityFrames_;
        const std::size_t secondFrame = (readFrame_ + secondOffset) % capacityFrames_;
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            const double first = static_cast<double>(data_[firstFrame * kChannels + channel]);
            const double second = static_cast<double>(data_[secondFrame * kChannels + channel]);
            const double value = first + (second - first) * fraction;
            interleaved[frame * kChannels + channel] = static_cast<std::int32_t>(std::clamp(
                std::llround(value),
                static_cast<long long>(std::numeric_limits<std::int32_t>::min()),
                static_cast<long long>(std::numeric_limits<std::int32_t>::max())));
        }
    }

    const double nextPosition =
        phase + static_cast<double>(outputFrames) * inputFramesPerOutputFrame;
    const std::size_t consumedFrames = static_cast<std::size_t>(nextPosition);
    phase = nextPosition - static_cast<double>(consumedFrames);
    readFrame_ = (readFrame_ + consumedFrames) % capacityFrames_;
    sizeFrames_ -= consumedFrames;
    return outputFrames;
}

bool WasapiBackend::probe(std::string& error) {
    ComPtr<IMMDevice> render;
    ComPtr<IMMDevice> capture;
    return findEndpoint(eRender, render, error) && findEndpoint(eCapture, capture, error);
}

bool WasapiBackend::findEndpoint(EDataFlow flow, ComPtr<IMMDevice>& device, std::string& error) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        error = hresultMessage("MMDeviceEnumerator", hr);
        return false;
    }

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        error = hresultMessage("EnumAudioEndpoints", hr);
        return false;
    }

    UINT count = 0;
    collection->GetCount(&count);
    const std::wstring wanted = upper(kSonulabUsbHardwareId);
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> candidate;
        if (FAILED(collection->Item(index, &candidate))) {
            continue;
        }

        if (endpointBelongsToSonulab(candidate.Get())) {
            device = candidate;
            return true;
        }

        ComPtr<IPropertyStore> properties;
        if (FAILED(candidate->OpenPropertyStore(STGM_READ, &properties))) {
            continue;
        }

        const std::array<PROPERTYKEY, 2> identityKeys{
            PKEY_Device_Parent,
            PKEY_Device_InstanceId,
        };
        for (const PROPERTYKEY& identityKey : identityKeys) {
            PROPVARIANT identity;
            PropVariantInit(&identity);
            const HRESULT propertyHr = properties->GetValue(identityKey, &identity);
            if (SUCCEEDED(propertyHr) && identity.vt == VT_LPWSTR && identity.pwszVal) {
                const std::wstring id = upper(identity.pwszVal);
                if (id.find(wanted) != std::wstring::npos) {
                    device = candidate;
                    PropVariantClear(&identity);
                    return true;
                }
            }
            PropVariantClear(&identity);
        }
    }

    error = flow == eRender
        ? "StompPRO playback endpoint (VID_1D6B/PID_0104) not found"
        : "StompPRO capture endpoint (VID_1D6B/PID_0104) not found";
    return false;
}

bool WasapiBackend::initializeEndpoint(
    IMMDevice* device,
    ComPtr<IAudioClient>& client,
    HANDLE eventHandle,
    std::uint32_t& endpointFrames,
    std::string& error) {
    const auto format = makeFormat();

    auto activate = [&]() -> HRESULT {
        client.Reset();
        return device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    };

    HRESULT hr = activate();
    if (FAILED(hr)) {
        error = hresultMessage("Activate IAudioClient", hr);
        return false;
    }

    hr = client->IsFormatSupported(
        AUDCLNT_SHAREMODE_EXCLUSIVE,
        reinterpret_cast<const WAVEFORMATEX*>(&format),
        nullptr);
    if (hr != S_OK) {
        error = hresultMessage("48 kHz stereo S32 exclusive format", hr);
        return false;
    }

    REFERENCE_TIME defaultPeriod = 0;
    REFERENCE_TIME minimumPeriod = 0;
    hr = client->GetDevicePeriod(&defaultPeriod, &minimumPeriod);
    if (FAILED(hr)) {
        error = hresultMessage("GetDevicePeriod", hr);
        return false;
    }

    REFERENCE_TIME requestedPeriod = std::max<REFERENCE_TIME>(minimumPeriod, 10'000);
    hr = client->Initialize(
        AUDCLNT_SHAREMODE_EXCLUSIVE,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
        requestedPeriod,
        requestedPeriod,
        reinterpret_cast<const WAVEFORMATEX*>(&format),
        nullptr);

    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        UINT32 alignedFrames = 0;
        if (FAILED(client->GetBufferSize(&alignedFrames)) || alignedFrames == 0) {
            error = hresultMessage("Get aligned endpoint buffer size", hr);
            return false;
        }
        requestedPeriod = static_cast<REFERENCE_TIME>(
            (static_cast<std::uint64_t>(alignedFrames) * kReferenceTimePerSecond + kSampleRate - 1) /
            kSampleRate);
        hr = activate();
        if (SUCCEEDED(hr)) {
            hr = client->Initialize(
                AUDCLNT_SHAREMODE_EXCLUSIVE,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
                requestedPeriod,
                requestedPeriod,
                reinterpret_cast<const WAVEFORMATEX*>(&format),
                nullptr);
        }
    }

    if (FAILED(hr)) {
        error = hresultMessage("Initialize exclusive event stream", hr);
        return false;
    }

    hr = client->SetEventHandle(eventHandle);
    if (FAILED(hr)) {
        error = hresultMessage("SetEventHandle", hr);
        return false;
    }

    UINT32 frames = 0;
    hr = client->GetBufferSize(&frames);
    if (FAILED(hr) || frames == 0) {
        error = hresultMessage("GetBufferSize", hr);
        return false;
    }
    endpointFrames = frames;
    return true;
}

bool WasapiBackend::open(std::uint32_t asioBlockFrames, CycleCallback callback, std::string& error) {
    close();
    if (!callback || asioBlockFrames == 0) {
        error = "Invalid ASIO stream configuration";
        return false;
    }

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    renderEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    captureEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stopEvent_ || !renderEvent_ || !captureEvent_) {
        error = "Unable to create audio synchronization events";
        close();
        return false;
    }

    if (!findEndpoint(eRender, renderDevice_, error) ||
        !findEndpoint(eCapture, captureDevice_, error) ||
        !initializeEndpoint(renderDevice_.Get(), renderClient_, renderEvent_, renderEndpointFrames_, error) ||
        !initializeEndpoint(captureDevice_.Get(), captureClient_, captureEvent_, captureEndpointFrames_, error)) {
        close();
        return false;
    }

    HRESULT hr = renderClient_->GetService(IID_PPV_ARGS(&renderService_));
    if (FAILED(hr)) {
        error = hresultMessage("Get IAudioRenderClient", hr);
        close();
        return false;
    }
    hr = captureClient_->GetService(IID_PPV_ARGS(&captureService_));
    if (FAILED(hr)) {
        error = hresultMessage("Get IAudioCaptureClient", hr);
        close();
        return false;
    }

    asioBlockFrames_ = asioBlockFrames;
    callback_ = std::move(callback);
    const std::size_t ringFrames = std::max<std::size_t>(
        65'536,
        static_cast<std::size_t>(asioBlockFrames_) * 32);
    captureRing_.reset(ringFrames);
    renderRing_.reset(ringFrames);
    inputBlock_.assign(static_cast<std::size_t>(asioBlockFrames_) * kChannels, 0);
    outputBlock_.assign(static_cast<std::size_t>(asioBlockFrames_) * kChannels, 0);
    captureScratch_.assign(
        static_cast<std::size_t>(std::max(captureEndpointFrames_, renderEndpointFrames_)) * kChannels,
        0);
    samplePosition_ = 0;
    pendingDiscontinuity_ = false;
    captureTargetFrames_ =
        static_cast<std::size_t>(asioBlockFrames_) * 2 + captureEndpointFrames_;
    captureLevelFiltered_ = static_cast<double>(captureTargetFrames_);
    captureResamplePhase_ = 0.0;
    gCapturePackets.store(0, std::memory_order_relaxed);
    gCaptureFrames.store(0, std::memory_order_relaxed);
    gCaptureRawZeroFrames.store(0, std::memory_order_relaxed);
    gCaptureSilentFrames.store(0, std::memory_order_relaxed);
    gCaptureDiscontinuities.store(0, std::memory_order_relaxed);
    gInputUnderflowFrames.store(0, std::memory_order_relaxed);
    gRenderEvents.store(0, std::memory_order_relaxed);
    gRenderTimeouts.store(0, std::memory_order_relaxed);
    gRenderUnderflowFrames.store(0, std::memory_order_relaxed);
    gMinimumCaptureLevel.store(std::numeric_limits<std::int64_t>::max(), std::memory_order_relaxed);
    gMaximumCaptureLevel.store(0, std::memory_order_relaxed);
    gMinimumRatePpm.store(0, std::memory_order_relaxed);
    gMaximumRatePpm.store(0, std::memory_order_relaxed);
    return true;
}

bool WasapiBackend::start(std::string& error) {
    if (!renderClient_ || !captureClient_ || running_.load()) {
        error = "Audio stream is not open or is already running";
        return false;
    }

    ResetEvent(stopEvent_);
    captureRing_.clear();
    renderRing_.clear();
    samplePosition_ = 0;
    pendingDiscontinuity_ = false;

    // Capture and playback can expose slightly different effective clocks even
    // when the UAC2 function advertises one nominal 48 kHz source. Prime the
    // capture side to the latency reported to ASIO, then keep that reservoir
    // centered with a very small adaptive resampling ratio.
    HRESULT hr = captureClient_->Start();
    if (FAILED(hr)) {
        error = hresultMessage("Start capture stream", hr);
        captureClient_->Reset();
        return false;
    }

    const DWORD captureWaitMs = static_cast<DWORD>(std::max<std::uint32_t>(
        1,
        (captureEndpointFrames_ * 2'000U + kSampleRate - 1) / kSampleRate));
    const ULONGLONG prefillDeadline = GetTickCount64() + 2'000;
    bool startupDiscontinuity = false;
    while (captureRing_.sizeFrames() < captureTargetFrames_ &&
           GetTickCount64() < prefillDeadline) {
        const DWORD waitResult = WaitForSingleObject(captureEvent_, captureWaitMs);
        if (waitResult == WAIT_OBJECT_0) {
            drainCapture(startupDiscontinuity);
        } else if (waitResult == WAIT_FAILED) {
            break;
        }
    }
    if (captureRing_.sizeFrames() < captureTargetFrames_) {
        captureClient_->Stop();
        captureClient_->Reset();
        error = "Capture stream did not provide enough data during startup";
        return false;
    }

    captureLevelFiltered_ = static_cast<double>(captureRing_.sizeFrames());
    captureResamplePhase_ = 0.0;
    pendingDiscontinuity_ = false;
    running_.store(true);
    audioThread_ = std::thread(&WasapiBackend::threadMain, this);

    hr = renderClient_->Start();
    if (FAILED(hr)) {
        error = hresultMessage("Start full-duplex stream", hr);
        stop();
        return false;
    }
    return true;
}

void WasapiBackend::stop() {
    if (!running_.exchange(false)) {
        if (audioThread_.joinable()) {
            SetEvent(stopEvent_);
            audioThread_.join();
        }
        return;
    }

    SetEvent(stopEvent_);
    if (audioThread_.joinable()) {
        audioThread_.join();
    }
    if (renderClient_) {
        renderClient_->Stop();
        renderClient_->Reset();
    }
    if (captureClient_) {
        captureClient_->Stop();
        captureClient_->Reset();
    }
}

void WasapiBackend::close() {
    stop();
    captureService_.Reset();
    renderService_.Reset();
    captureClient_.Reset();
    renderClient_.Reset();
    captureDevice_.Reset();
    renderDevice_.Reset();
    callback_ = {};
    asioBlockFrames_ = 0;
    renderEndpointFrames_ = 0;
    captureEndpointFrames_ = 0;
    if (captureEvent_) {
        CloseHandle(captureEvent_);
        captureEvent_ = nullptr;
    }
    if (renderEvent_) {
        CloseHandle(renderEvent_);
        renderEvent_ = nullptr;
    }
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

void WasapiBackend::threadMain() {
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (mmcss) {
        AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_CRITICAL);
    }

    const std::array<HANDLE, 3> handles{stopEvent_, renderEvent_, captureEvent_};
    const DWORD fallbackTimeoutMs = static_cast<DWORD>(std::max<std::uint32_t>(
        1,
        (renderEndpointFrames_ * 2'000U + kSampleRate - 1) / kSampleRate));
    makeAsioBlock(false);
    while (running_.load(std::memory_order_acquire)) {
        const DWORD result = WaitForMultipleObjects(
            static_cast<DWORD>(handles.size()), handles.data(), FALSE, fallbackTimeoutMs);
        if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) {
            break;
        }

        if (result == WAIT_OBJECT_0 + 1) {
            gRenderEvents.fetch_add(1, std::memory_order_relaxed);
        } else if (result == WAIT_TIMEOUT) {
            gRenderTimeouts.fetch_add(1, std::memory_order_relaxed);
        }

        bool discontinuity = pendingDiscontinuity_;
        drainCapture(discontinuity);
        pendingDiscontinuity_ = false;

        if (result == WAIT_OBJECT_0 + 1 || result == WAIT_TIMEOUT) {
            renderAvailableFrames(discontinuity, result == WAIT_OBJECT_0 + 1);
        } else if (discontinuity) {
            pendingDiscontinuity_ = true;
        }
    }

    if (mmcss) {
        AvRevertMmThreadCharacteristics(mmcss);
    }
    if (SUCCEEDED(coHr)) {
        CoUninitialize();
    }
}

void WasapiBackend::drainCapture(bool& discontinuity) noexcept {
    if (!captureService_) {
        return;
    }

    UINT32 packetFrames = 0;
    const std::size_t maximumDrainFrames = std::max<std::size_t>(
        static_cast<std::size_t>(asioBlockFrames_) * 4,
        static_cast<std::size_t>(captureEndpointFrames_) * 8);
    std::size_t drainedFrames = 0;
    std::size_t drainedPackets = 0;
    while (drainedFrames < maximumDrainFrames && drainedPackets < 64 &&
           SUCCEEDED(captureService_->GetNextPacketSize(&packetFrames)) &&
           packetFrames > 0) {
        BYTE* bytes = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        const HRESULT hr = captureService_->GetBuffer(
            &bytes, &frames, &flags, nullptr, nullptr);
        if (FAILED(hr)) {
            discontinuity = true;
            return;
        }

        if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) {
            discontinuity = true;
            gCaptureDiscontinuities.fetch_add(1, std::memory_order_relaxed);
        }

        gCapturePackets.fetch_add(1, std::memory_order_relaxed);
        gCaptureFrames.fetch_add(frames, std::memory_order_relaxed);

        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || bytes == nullptr) {
            gCaptureSilentFrames.fetch_add(frames, std::memory_order_relaxed);
            std::size_t remaining = frames;
            std::fill(captureScratch_.begin(), captureScratch_.end(), 0);
            const std::size_t scratchFrames = captureScratch_.size() / kChannels;
            while (remaining > 0) {
                const std::size_t chunk = std::min(remaining, scratchFrames);
                captureRing_.push(captureScratch_.data(), chunk);
                remaining -= chunk;
            }
        } else {
            const auto* samples = reinterpret_cast<const std::int32_t*>(bytes);
            std::uint64_t zeroFrames = 0;
            for (UINT32 frame = 0; frame < frames; ++frame) {
                if (samples[static_cast<std::size_t>(frame) * kChannels] == 0 &&
                    samples[static_cast<std::size_t>(frame) * kChannels + 1] == 0) {
                    ++zeroFrames;
                }
            }
            gCaptureRawZeroFrames.fetch_add(zeroFrames, std::memory_order_relaxed);
            captureRing_.push(samples, frames);
        }
        captureService_->ReleaseBuffer(frames);
        drainedFrames += frames;
        ++drainedPackets;
    }
}

void WasapiBackend::renderAvailableFrames(bool discontinuity, bool endpointEvent) noexcept {
    if (!renderClient_ || !renderService_) {
        return;
    }

    UINT32 available = 0;
    if (endpointEvent) {
        // In exclusive event-driven mode each render event requests exactly one
        // complete endpoint packet. GetCurrentPadding follows shared-mode
        // semantics and remains equal to the endpoint buffer size on this UAC2
        // device, so it cannot be used to determine writable frames here.
        available = renderEndpointFrames_;
    } else {
        UINT32 padding = 0;
        if (FAILED(renderClient_->GetCurrentPadding(&padding)) || padding > renderEndpointFrames_) {
            pendingDiscontinuity_ = true;
            return;
        }
        available = renderEndpointFrames_ - padding;
    }
    if (available == 0) {
        return;
    }

    while (renderRing_.sizeFrames() < available) {
        makeAsioBlock(discontinuity);
        discontinuity = false;
    }

    BYTE* bytes = nullptr;
    if (FAILED(renderService_->GetBuffer(available, &bytes)) || !bytes) {
        pendingDiscontinuity_ = true;
        return;
    }
    auto* samples = reinterpret_cast<std::int32_t*>(bytes);
    const std::size_t popped = renderRing_.pop(samples, available);
    if (popped < available) {
        gRenderUnderflowFrames.fetch_add(available - popped, std::memory_order_relaxed);
        std::fill(
            samples + popped * kChannels,
            samples + static_cast<std::size_t>(available) * kChannels,
            0);
        pendingDiscontinuity_ = true;
    }
    if (FAILED(renderService_->ReleaseBuffer(available, 0))) {
        pendingDiscontinuity_ = true;
    }
}

void WasapiBackend::makeAsioBlock(bool discontinuity) noexcept {
    const std::size_t available = captureRing_.sizeFrames();
    updateMinimum(gMinimumCaptureLevel, static_cast<std::int64_t>(available));
    updateMaximum(gMaximumCaptureLevel, static_cast<std::int64_t>(available));

    constexpr double kLevelFilter = 0.01;
    constexpr double kProportionalGain = 0.0005;
    captureLevelFiltered_ +=
        (static_cast<double>(available) - captureLevelFiltered_) * kLevelFilter;
    const double levelErrorBlocks =
        (captureLevelFiltered_ - static_cast<double>(captureTargetFrames_)) /
        static_cast<double>(asioBlockFrames_);
    const double captureRate = std::clamp(
        1.0 + kProportionalGain * levelErrorBlocks,
        0.998,
        1.002);
    const auto ratePpm = static_cast<std::int64_t>(std::llround((captureRate - 1.0) * 1'000'000.0));
    updateMinimum(gMinimumRatePpm, ratePpm);
    updateMaximum(gMaximumRatePpm, ratePpm);

    const std::size_t captured = captureRing_.resamplePop(
        inputBlock_.data(), asioBlockFrames_, captureRate, captureResamplePhase_);
    if (captured < asioBlockFrames_) {
        captureResamplePhase_ = 0.0;
        const std::size_t fallback = captureRing_.pop(inputBlock_.data(), asioBlockFrames_);
        if (fallback < asioBlockFrames_) {
            discontinuity = true;
            gInputUnderflowFrames.fetch_add(
                asioBlockFrames_ - fallback, std::memory_order_relaxed);
            std::fill(
                inputBlock_.begin() + static_cast<std::ptrdiff_t>(fallback * kChannels),
                inputBlock_.end(),
                0);
        }
    }
    std::fill(outputBlock_.begin(), outputBlock_.end(), 0);

    callback_(
        inputBlock_.data(),
        outputBlock_.data(),
        asioBlockFrames_,
        samplePosition_,
        qpcNanoseconds(),
        discontinuity);

    renderRing_.push(outputBlock_.data(), asioBlockFrames_);
    samplePosition_ += asioBlockFrames_;
}

std::uint64_t WasapiBackend::qpcNanoseconds() noexcept {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    const long double ns = static_cast<long double>(counter.QuadPart) * 1'000'000'000.0L /
        static_cast<long double>(frequency.QuadPart);
    return static_cast<std::uint64_t>(ns);
}

std::string WasapiBackend::hresultMessage(const char* operation, HRESULT hr) {
    std::ostringstream stream;
    stream << operation << " failed (HRESULT 0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(hr) << ')';
    return stream.str();
}

extern "C" __declspec(dllexport) void __stdcall SonulabGetDiagnostics(
    std::uint64_t* values,
    std::uint32_t valueCount) {
    if (!values || valueCount < 12) {
        return;
    }
    values[0] = gCapturePackets.load(std::memory_order_relaxed);
    values[1] = gCaptureFrames.load(std::memory_order_relaxed);
    values[2] = gCaptureRawZeroFrames.load(std::memory_order_relaxed);
    values[3] = gCaptureSilentFrames.load(std::memory_order_relaxed);
    values[4] = gCaptureDiscontinuities.load(std::memory_order_relaxed);
    values[5] = gInputUnderflowFrames.load(std::memory_order_relaxed);
    values[6] = gRenderEvents.load(std::memory_order_relaxed);
    values[7] = gRenderTimeouts.load(std::memory_order_relaxed);
    values[8] = gRenderUnderflowFrames.load(std::memory_order_relaxed);
    values[9] = static_cast<std::uint64_t>(gMinimumCaptureLevel.load(std::memory_order_relaxed));
    values[10] = static_cast<std::uint64_t>(gMaximumCaptureLevel.load(std::memory_order_relaxed));
    const auto minPpm = static_cast<std::int32_t>(gMinimumRatePpm.load(std::memory_order_relaxed));
    const auto maxPpm = static_cast<std::int32_t>(gMaximumRatePpm.load(std::memory_order_relaxed));
    values[11] = static_cast<std::uint32_t>(minPpm) |
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(maxPpm)) << 32);
}
