#include "audio_backend.h"

#include "runtime_log.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

namespace {

// BASARA_AUDIO_STRETCH=0: push straight into SDL's queue (the old path, no time stretching).
bool StretchWanted() {
    const char* value = std::getenv("BASARA_AUDIO_STRETCH");
    return value == nullptr || std::strcmp(value, "0") != 0;
}

// No pull from the device for this long while the game is pushing audio: the output was lost (on
// Android AAudio reports AAUDIO_ERROR_DISCONNECTED on a route change and may stay silent).
constexpr uint64_t kStalledPullMs = 1500;
constexpr uint64_t kReopenIntervalMs = 3000;

} // namespace

AudioBackend& AudioBackend::Instance() {
    static AudioBackend instance;
    return instance;
}

float AudioBackend::EffectiveGainLocked() const {
    return m_muted ? 0.0f : m_masterVolume;
}

void AudioBackend::ApplyGainLocked() {
    if (m_stream && !SDL_SetAudioStreamGain(m_stream, EffectiveGainLocked())) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_SetAudioStreamGain failed: " << SDL_GetError() << std::endl;
    }
}

void AudioBackend::SetMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
    ApplyGainLocked();
}

void AudioBackend::SetMuted(bool muted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_muted = muted;
    ApplyGainLocked();
}

void SDLCALL AudioBackend::PullCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int) {
    if (additionalAmount <= 0) {
        return;
    }
    auto* self = static_cast<AudioBackend*>(userdata);
    const size_t frames = (static_cast<size_t>(additionalAmount) + 3) / 4;
    self->m_pullBuffer.resize(frames * 2);
    {
        std::lock_guard<std::mutex> lock(self->m_stretchMutex);
        self->m_stretcher.Pull(self->m_pullBuffer.data(), frames);
    }
    self->m_lastPullMs.store(SDL_GetTicks(), std::memory_order_relaxed);
    SDL_PutAudioStreamData(stream, self->m_pullBuffer.data(), static_cast<int>(frames * 4));
}

SDL_AudioStream* AudioBackend::OpenStreamLocked(const SDL_AudioSpec& spec) {
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                                        m_stretch ? &AudioBackend::PullCallback : nullptr, this);
    if (!stream) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_OpenAudioDeviceStream failed: " << SDL_GetError() << std::endl;
        return nullptr;
    }

    if (!SDL_ResumeAudioStreamDevice(stream)) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_ResumeAudioStreamDevice failed: " << SDL_GetError() << std::endl;
        SDL_DestroyAudioStream(stream);
        return nullptr;
    }

    if (!SDL_SetAudioStreamGain(stream, EffectiveGainLocked())) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_SetAudioStreamGain failed: " << SDL_GetError() << std::endl;
        SDL_DestroyAudioStream(stream);
        return nullptr;
    }
    m_lastPullMs.store(SDL_GetTicks(), std::memory_order_relaxed);
    return stream;
}

bool AudioBackend::EnsureInitializedLocked(uint32_t sampleRate, uint32_t channels) {
    if (m_initialized && m_sampleRate == sampleRate && m_channels == channels) {
        return true;
    }

    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_InitSubSystem(SDL_INIT_AUDIO) failed: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = static_cast<int>(channels);
    spec.freq = static_cast<int>(sampleRate);

    m_stretch = channels == 2 && StretchWanted();
    if (m_stretch) {
        std::lock_guard<std::mutex> lock(m_stretchMutex);
        m_stretcher.Reset(sampleRate);
    }
    RT_LOGF(RT_TAG_AUDIO, "output %u Hz, %s\n", sampleRate,
            m_stretch ? "time stretching on" : "time stretching off (BASARA_AUDIO_STRETCH=0)");

    SDL_AudioStream* stream = OpenStreamLocked(spec);
    if (!stream) {
        return false;
    }

    m_stream = stream;
    m_spec = spec;
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_initialized = true;
    return true;
}

bool AudioBackend::Init(uint32_t sampleRate, uint32_t channels) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return EnsureInitializedLocked(sampleRate, channels);
}

void AudioBackend::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    if (m_initialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    m_initialized = false;
    m_sampleRate = 0;
    m_channels = 0;
    m_reportedDroppedBlock = false;
    m_convertBuffer.clear();
}

uint32_t AudioBackend::QueueLimitBytesLocked() const {
    // A cap, not a target: the queue normally sits near 70 ms. A slow phone (Helio G99 in a
    // crowded battle) hands over audio in bursts, and a 120 ms cap dropped dozens of blocks a second.
    constexpr uint32_t queueMs = 200;

    const uint64_t bytesPerSecond = static_cast<uint64_t>(m_spec.freq) *
                                    static_cast<uint64_t>(m_spec.channels) *
                                    sizeof(int16_t);
    return static_cast<uint32_t>((bytesPerSecond * queueMs) / 1000u);
}

bool AudioBackend::QueueHasCapacityLocked(int incomingBytes) {
    if (!m_stream) {
        return false;
    }
    const int queued = SDL_GetAudioStreamQueued(m_stream);
    if (queued < 0) {
        return true;
    }
    ++m_statPushes;
    m_statEmpty += queued == 0 ? 1 : 0;
    m_statMinQueued = m_statMinQueued < 0 ? queued : std::min(m_statMinQueued, queued);
    const uint32_t maxQueued = QueueLimitBytesLocked();
    if (static_cast<uint64_t>(queued) + static_cast<uint64_t>(std::max(incomingBytes, 0)) > maxQueued) {
        ++m_statDropped;
        // Match Dolphin's FIFO overflow behavior: preserve the continuous audio
        // already queued and discard the new block.  Clearing SDL's entire
        // stream creates an audible discontinuity (the severe crackle seen when
        // VI-batched DMA briefly outran playback).
        // A drop is audible; report the first one so it is not invisible.
        if (!m_reportedDroppedBlock) {
            m_reportedDroppedBlock = true;
            RT_LOG(RT_TAG_AUDIO) << "output queue full (" << queued << "/" << maxQueued
                      << " bytes); dropping blocks to preserve continuity" << std::endl;
        }
        return false;
    }
    m_statPushedBytes += static_cast<uint64_t>(std::max(incomingBytes, 0));
    return true;
}

void AudioBackend::ReopenIfStalledLocked() {
    const uint64_t now = SDL_GetTicks();
    // The audio thread can store a pull time later than `now`: never subtract past zero.
    const uint64_t lastPull = m_lastPullMs.load(std::memory_order_relaxed);
    if (now <= lastPull || now - lastPull < kStalledPullMs || now - m_lastReopenMs < kReopenIntervalMs) {
        return;
    }
    m_lastReopenMs = now;
    ++m_statReopens;
    RT_LOGF(RT_TAG_AUDIO, "output stalled (no pull for %llu ms): reopening the audio device\n",
            static_cast<unsigned long long>(now - lastPull));
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
    }
    m_stream = OpenStreamLocked(m_spec);
}

bool AudioBackend::PushFramesLocked(const int16_t* frames, size_t frameCount) {
    const int lenBytes = static_cast<int>(frameCount * 2 * sizeof(int16_t));
    if (m_stretch) {
        ReopenIfStalledLocked();
        std::lock_guard<std::mutex> lock(m_stretchMutex);
        m_stretcher.Push(frames, frameCount);
        ++m_statPushes;
        m_statPushedBytes += static_cast<uint64_t>(lenBytes);
        return true;
    }
    if (!QueueHasCapacityLocked(lenBytes)) {
        return true;
    }
    if (!SDL_PutAudioStreamData(m_stream, frames, lenBytes)) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_PutAudioStreamData failed: " << SDL_GetError() << std::endl;
        return false;
    }
    return true;
}

AudioBackend::Stats AudioBackend::TakeStats() {
    std::lock_guard<std::mutex> lock(m_mutex);
    Stats stats;
    const double bytesPerMs = m_spec.freq * m_spec.channels * sizeof(int16_t) / 1000.0;
    stats.stretching = m_stretch;
    stats.pushes = m_statPushes;
    stats.reopens = m_statReopens;
    if (m_stretch) {
        std::lock_guard<std::mutex> stretchLock(m_stretchMutex);
        const AudioStretcher::Stats s = m_stretcher.TakeStats();
        stats.empty = s.gaps;
        stats.dropped = s.skips;
        stats.minQueuedMs = s.minBacklogMs;
        stats.playedMs = s.playedMs;
        stats.minTempo = s.minTempo;
        stats.maxTempo = s.maxTempo;
        if (bytesPerMs > 0) {
            stats.pushedMs = static_cast<float>(m_statPushedBytes / bytesPerMs);
        }
    } else {
        stats.empty = m_statEmpty;
        stats.dropped = m_statDropped;
        if (bytesPerMs > 0) {
            stats.minQueuedMs = m_statMinQueued < 0 ? -1.0f : static_cast<float>(m_statMinQueued / bytesPerMs);
            stats.pushedMs = static_cast<float>(m_statPushedBytes / bytesPerMs);
        }
    }
    m_statPushes = m_statEmpty = m_statDropped = m_statReopens = 0;
    m_statMinQueued = -1;
    m_statPushedBytes = 0;
    return stats;
}

bool AudioBackend::PushWiiAiSamplesBE16(const uint8_t* data, size_t bytes) {
    if (!data || bytes == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
        return false;
    }

    const size_t sampleCount = bytes / sizeof(int16_t);
    if (sampleCount == 0) {
        return false;
    }

    if (m_convertBuffer.size() < sampleCount) {
        m_convertBuffer.resize(sampleCount);
    }

    const size_t frameCount = sampleCount / 2;
    for (size_t frame = 0; frame < frameCount; ++frame) {
        const size_t rightOffset = frame * 4;
        const size_t leftOffset = rightOffset + 2;
        const uint16_t right = static_cast<uint16_t>(data[rightOffset]) << 8 |
                               static_cast<uint16_t>(data[rightOffset + 1]);
        const uint16_t left = static_cast<uint16_t>(data[leftOffset]) << 8 |
                              static_cast<uint16_t>(data[leftOffset + 1]);
        m_convertBuffer[frame * 2] = static_cast<int16_t>(left);
        m_convertBuffer[frame * 2 + 1] = static_cast<int16_t>(right);
    }

    return PushFramesLocked(m_convertBuffer.data(), frameCount);
}

bool AudioBackend::PushSamplesLE16(const int16_t* samples, size_t sampleCount) {
    if (!samples || sampleCount == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
        return false;
    }

    return PushFramesLocked(samples, sampleCount / 2);
}
