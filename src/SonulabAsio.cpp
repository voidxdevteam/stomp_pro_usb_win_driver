#include "SonulabAsio.hpp"

#include "SonulabIds.hpp"

#include <algorithm>
#include <cstring>

SonulabAsio::SonulabAsio() {
    sonulabObjectCreated();
}

SonulabAsio::~SonulabAsio() {
    stop();
    disposeBuffers();
    sonulabObjectDestroyed();
}

HRESULT STDMETHODCALLTYPE SonulabAsio::QueryInterface(REFIID riid, void** object) {
    if (!object) {
        return E_POINTER;
    }
    *object = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, CLSID_SonulabASIO)) {
        *object = static_cast<IASIO*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE SonulabAsio::AddRef() {
    return references_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE SonulabAsio::Release() {
    const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

ASIOBool SonulabAsio::init(void* sysHandle) {
    (void)sysHandle;
    std::string error;
    initialized_ = backend_.probe(error);
    if (!initialized_) {
        setError(std::move(error));
        return ASIOFalse;
    }
    error_.clear();
    return ASIOTrue;
}

void SonulabAsio::getDriverName(char* name) {
    copyAsioString(name, 32, "Sonulab ASIO");
}

long SonulabAsio::getDriverVersion() {
    return 0x00010000;
}

void SonulabAsio::getErrorMessage(char* string) {
    copyAsioString(string, 124, error_.empty() ? "No error" : error_.c_str());
}

ASIOError SonulabAsio::start() {
    if (!initialized_ || !callbacks_ || allocations_.empty()) {
        setError("Driver is not initialized or ASIO buffers have not been created");
        return ASE_InvalidMode;
    }
    if (running_) {
        return ASE_OK;
    }

    bufferIndex_ = 0;
    samplePosition_.store(0);
    timestampNs_.store(0);
    std::string error;
    const bool opened = backend_.open(
        static_cast<std::uint32_t>(blockFrames_),
        [this](const std::int32_t* input,
               std::int32_t* output,
               std::uint32_t frames,
               std::uint64_t position,
               std::uint64_t timestamp,
               bool discontinuity) {
            processCycle(input, output, frames, position, timestamp, discontinuity);
        },
        error);
    if (!opened || !backend_.start(error)) {
        setError(std::move(error));
        backend_.close();
        return ASE_HWMalfunction;
    }

    running_ = true;
    error_.clear();
    return ASE_OK;
}

ASIOError SonulabAsio::stop() {
    backend_.stop();
    backend_.close();
    running_ = false;
    return ASE_OK;
}

ASIOError SonulabAsio::getChannels(long* numInputChannels, long* numOutputChannels) {
    if (!numInputChannels || !numOutputChannels) {
        return ASE_InvalidParameter;
    }
    *numInputChannels = kChannels;
    *numOutputChannels = kChannels;
    return ASE_OK;
}

ASIOError SonulabAsio::getLatencies(long* inputLatency, long* outputLatency) {
    if (!inputLatency || !outputLatency) {
        return ASE_InvalidParameter;
    }
    *inputLatency = blockFrames_ * 2 + static_cast<long>(backend_.captureEndpointFrames());
    *outputLatency = blockFrames_ + static_cast<long>(backend_.renderEndpointFrames());
    return ASE_OK;
}

ASIOError SonulabAsio::getBufferSize(
    long* minSize,
    long* maxSize,
    long* preferredSize,
    long* granularity) {
    if (!minSize || !maxSize || !preferredSize || !granularity) {
        return ASE_InvalidParameter;
    }
    *minSize = kBufferSizes.front();
    *maxSize = kBufferSizes.back();
    *preferredSize = 256;
    *granularity = -1;
    return ASE_OK;
}

ASIOError SonulabAsio::canSampleRate(ASIOSampleRate sampleRate) {
    return sampleRate == kSampleRate ? ASE_OK : ASE_NoClock;
}

ASIOError SonulabAsio::getSampleRate(ASIOSampleRate* sampleRate) {
    if (!sampleRate) {
        return ASE_InvalidParameter;
    }
    *sampleRate = kSampleRate;
    return ASE_OK;
}

ASIOError SonulabAsio::setSampleRate(ASIOSampleRate sampleRate) {
    return canSampleRate(sampleRate);
}

ASIOError SonulabAsio::getClockSources(ASIOClockSource* clocks, long* numSources) {
    if (!clocks || !numSources || *numSources < 1) {
        return ASE_InvalidParameter;
    }
    clocks[0].index = 0;
    clocks[0].associatedChannel = -1;
    clocks[0].associatedGroup = -1;
    clocks[0].isCurrentSource = ASIOTrue;
    copyAsioString(clocks[0].name, sizeof(clocks[0].name), "StompPRO USB clock");
    *numSources = 1;
    return ASE_OK;
}

ASIOError SonulabAsio::setClockSource(long reference) {
    return reference == 0 ? ASE_OK : ASE_NotPresent;
}

ASIOError SonulabAsio::getSamplePosition(ASIOSamples* samplePosition, ASIOTimeStamp* timestamp) {
    if (!samplePosition || !timestamp) {
        return ASE_InvalidParameter;
    }
    setAsio64(*samplePosition, samplePosition_.load(std::memory_order_relaxed));
    setAsio64(*timestamp, timestampNs_.load(std::memory_order_relaxed));
    return ASE_OK;
}

ASIOError SonulabAsio::getChannelInfo(ASIOChannelInfo* info) {
    if (!info || info->channel < 0 || info->channel >= kChannels) {
        return ASE_InvalidParameter;
    }
    info->channelGroup = 0;
    info->type = ASIOSTInt32LSB;
    if (info->isInput) {
        info->isActive = inputBuffers_[info->channel][0] ? ASIOTrue : ASIOFalse;
        const char* name = info->channel == 0 ? "Input 1" : "Input 2";
        copyAsioString(info->name, sizeof(info->name), name);
    } else {
        info->isActive = outputBuffers_[info->channel][0] ? ASIOTrue : ASIOFalse;
        const char* name = info->channel == 0 ? "Output 1" : "Output 2";
        copyAsioString(info->name, sizeof(info->name), name);
    }
    return ASE_OK;
}

ASIOError SonulabAsio::createBuffers(
    ASIOBufferInfo* bufferInfos,
    long numChannels,
    long bufferSize,
    ASIOCallbacks* callbacks) {
    if (!bufferInfos || !callbacks || numChannels <= 0 || running_) {
        return ASE_InvalidParameter;
    }
    if (std::find(kBufferSizes.begin(), kBufferSizes.end(), bufferSize) == kBufferSizes.end()) {
        setError("Supported ASIO buffer sizes are 128, 256 and 512 samples");
        return ASE_InvalidParameter;
    }

    disposeBuffers();
    blockFrames_ = bufferSize;
    callbacks_ = callbacks;
    allocations_.reserve(static_cast<std::size_t>(numChannels) * 2);

    for (long index = 0; index < numChannels; ++index) {
        ASIOBufferInfo& info = bufferInfos[index];
        if (info.channelNum < 0 || info.channelNum >= kChannels) {
            disposeBuffers();
            return ASE_InvalidParameter;
        }

        auto& channelBuffers = info.isInput
            ? inputBuffers_[info.channelNum]
            : outputBuffers_[info.channelNum];
        if (channelBuffers[0] != nullptr) {
            disposeBuffers();
            return ASE_InvalidParameter;
        }

        for (long half = 0; half < 2; ++half) {
            auto allocation = std::make_unique<std::int32_t[]>(static_cast<std::size_t>(blockFrames_));
            std::fill_n(allocation.get(), blockFrames_, 0);
            info.buffers[half] = allocation.get();
            channelBuffers[half] = allocation.get();
            allocations_.push_back(std::move(allocation));
        }
    }

    std::memset(&timeInfo_, 0, sizeof(timeInfo_));
    timeInfo_.timeInfo.speed = 1.0;
    timeInfo_.timeInfo.sampleRate = kSampleRate;
    timeInfoMode_ = callbacks_->asioMessage && callbacks_->bufferSwitchTimeInfo &&
        callbacks_->asioMessage(kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
    return ASE_OK;
}

ASIOError SonulabAsio::disposeBuffers() {
    if (running_) {
        stop();
    }
    for (auto& channel : inputBuffers_) {
        channel = {nullptr, nullptr};
    }
    for (auto& channel : outputBuffers_) {
        channel = {nullptr, nullptr};
    }
    allocations_.clear();
    callbacks_ = nullptr;
    timeInfoMode_ = false;
    return ASE_OK;
}

ASIOError SonulabAsio::controlPanel() {
    const std::string message =
        "Sonulab StompPRO ASIO\n\n"
        "USB Audio 2.0 full duplex\n"
        "2 inputs / 2 outputs\n"
        "48 kHz, 32-bit\n"
        "Buffer: " + std::to_string(blockFrames_) + " samples";
    MessageBoxA(nullptr, message.c_str(), "Sonulab ASIO", MB_OK | MB_ICONINFORMATION);
    return ASE_OK;
}

ASIOError SonulabAsio::future(long selector, void* option) {
    (void)option;
    return selector == kAsioCanTimeInfo ? ASE_SUCCESS : ASE_NotPresent;
}

ASIOError SonulabAsio::outputReady() {
    return ASE_OK;
}

void SonulabAsio::processCycle(
    const std::int32_t* inputInterleaved,
    std::int32_t* outputInterleaved,
    std::uint32_t frames,
    std::uint64_t samplePosition,
    std::uint64_t timestampNs,
    bool discontinuity) noexcept {
    const long half = bufferIndex_;
    for (long channel = 0; channel < kChannels; ++channel) {
        if (inputBuffers_[channel][half]) {
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                inputBuffers_[channel][half][frame] = inputInterleaved[frame * kChannels + channel];
            }
        }
        if (outputBuffers_[channel][half]) {
            std::fill_n(outputBuffers_[channel][half], frames, 0);
        }
    }

    samplePosition_.store(samplePosition, std::memory_order_relaxed);
    timestampNs_.store(timestampNs, std::memory_order_relaxed);
    setAsio64(timeInfo_.timeInfo.samplePosition, samplePosition);
    setAsio64(timeInfo_.timeInfo.systemTime, timestampNs);
    timeInfo_.timeInfo.sampleRate = kSampleRate;
    timeInfo_.timeInfo.speed = 1.0;
    timeInfo_.timeInfo.flags =
        kSystemTimeValid | kSamplePositionValid | kSampleRateValid | kSpeedValid;

    if (discontinuity && callbacks_->asioMessage) {
        callbacks_->asioMessage(kAsioOverload, 0, nullptr, nullptr);
    }
    if (timeInfoMode_) {
        callbacks_->bufferSwitchTimeInfo(&timeInfo_, half, ASIOTrue);
    } else {
        callbacks_->bufferSwitch(half, ASIOTrue);
    }

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        for (long channel = 0; channel < kChannels; ++channel) {
            const auto* source = outputBuffers_[channel][half];
            outputInterleaved[frame * kChannels + channel] = source ? source[frame] : 0;
        }
    }
    bufferIndex_ ^= 1;
}

void SonulabAsio::setError(std::string message) {
    error_ = std::move(message);
}

void SonulabAsio::setAsio64(ASIOSamples& destination, std::uint64_t value) noexcept {
    destination.hi = static_cast<unsigned long>(value >> 32);
    destination.lo = static_cast<unsigned long>(value & 0xffffffffULL);
}

void SonulabAsio::setAsio64(ASIOTimeStamp& destination, std::uint64_t value) noexcept {
    destination.hi = static_cast<unsigned long>(value >> 32);
    destination.lo = static_cast<unsigned long>(value & 0xffffffffULL);
}

void SonulabAsio::copyAsioString(
    char* destination,
    std::size_t capacity,
    const char* source) noexcept {
    if (!destination || capacity == 0) {
        return;
    }
    strncpy_s(destination, capacity, source ? source : "", _TRUNCATE);
}
