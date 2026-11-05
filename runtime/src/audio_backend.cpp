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

#if defined(__ANDROID__)
// BASARA_AUDIO_API=sdl: the stretched output goes through SDL's AAudio backend (the old path).
bool DirectAAudioWanted() {
    const char* value = std::getenv("BASARA_AUDIO_API");
    return value == nullptr || std::strcmp(value, "sdl") != 0;
}

const char* PerfModeName(aaudio_performance_mode_t mode) {
    switch (mode) {
    case AAUDIO_PERFORMANCE_MODE_LOW_LATENCY: return "low latency";
    case AAUDIO_PERFORMANCE_MODE_POWER_SAVING: return "power saving";
    default: return "none";
    }
}
#endif

} // namespace

AudioBackend& AudioBackend::Instance() {
    static AudioBackend instance;
    return instance;
}

float AudioBackend::EffectiveGainLocked() const {
    return m_muted ? 0.0f : m_masterVolume;
}

void AudioBackend::ApplyGainLocked() {
    m_gain.store(EffectiveGainLocked(), std::memory_order_relaxed);
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
    self->PullStretched(self->m_pullBuffer.data(), frames);
    SDL_PutAudioStreamData(stream, self->m_pullBuffer.data(), static_cast<int>(frames * 4));
}

void AudioBackend::PullStretched(int16_t* out, size_t frames) {
    {
        std::lock_guard<std::mutex> lock(m_stretchMutex);
        m_stretcher.Pull(out, frames);
    }
    m_lastPullMs.store(SDL_GetTicks(), std::memory_order_relaxed);
}

#if defined(__ANDROID__)
aaudio_data_callback_result_t AudioBackend::AAudioData(AAudioStream*, void* userdata, void* audioData,
                                                       int32_t numFrames) {
    auto* self = static_cast<AudioBackend*>(userdata);
    if (audioData == nullptr || numFrames <= 0) {
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    auto* out = static_cast<int16_t*>(audioData);
    self->PullStretched(out, static_cast<size_t>(numFrames));
    const float gain = self->m_gain.load(std::memory_order_relaxed);
    if (gain < 0.999f) {
        const size_t samples = static_cast<size_t>(numFrames) * 2;
        for (size_t i = 0; i < samples; ++i) {
            out[i] = static_cast<int16_t>(static_cast<float>(out[i]) * gain);
        }
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// Runs on an AAudio thread, where the stream must not be closed: the game thread's next push reopens
// it (a disconnect is a route change: headphones, Bluetooth, or the device taken back by the system).
void AudioBackend::AAudioError(AAudioStream*, void* userdata, aaudio_result_t error) {
    auto* self = static_cast<AudioBackend*>(userdata);
    if (error == AAUDIO_ERROR_DISCONNECTED) {
        self->m_aaudioLost.store(true, std::memory_order_release);
    }
}

bool AudioBackend::TryOpenAAudioLocked(aaudio_performance_mode_t mode) {
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr) {
        return false;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder, mode);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(m_sampleRate));
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
    // No fixed callback size (ARMSX2): AAudio calls back with the device's native bursts, so the
    // output drains the stretcher as the device consumes instead of in large adapter-sized lumps.
    AAudioStreamBuilder_setBufferCapacityInFrames(builder, 4096);
    AAudioStreamBuilder_setDataCallback(builder, &AudioBackend::AAudioData, this);
    AAudioStreamBuilder_setErrorCallback(builder, &AudioBackend::AAudioError, this);
    AAudioStream* stream = nullptr;
    aaudio_result_t res = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (res != AAUDIO_OK || stream == nullptr) {
        RT_LOGF(RT_TAG_AUDIO, "AAudio open (%s) failed: %s\n", PerfModeName(mode), AAudio_convertResultToText(res));
        return false;
    }
    if (AAudioStream_getChannelCount(stream) != 2 || AAudioStream_getFormat(stream) != AAUDIO_FORMAT_PCM_I16) {
        RT_LOGF(RT_TAG_AUDIO, "AAudio opened %d channels, format %d: not usable\n",
                AAudioStream_getChannelCount(stream), static_cast<int>(AAudioStream_getFormat(stream)));
        AAudioStream_close(stream);
        return false;
    }
    m_aaudio = stream;
    m_aaudioLost.store(false, std::memory_order_release);
    m_lastPullMs.store(SDL_GetTicks(), std::memory_order_relaxed);
    if (!m_background) {
        res = AAudioStream_requestStart(stream);
        if (res != AAUDIO_OK) {
            RT_LOGF(RT_TAG_AUDIO, "AAudio start (%s) failed: %s\n", PerfModeName(mode),
                    AAudio_convertResultToText(res));
            CloseAAudioLocked();
            return false;
        }
    }
    // The device buffer starts at two bursts, the latency of the SDL path before it (the default is
    // the whole 4096-frame capacity: 128 ms at 32 kHz, audibly late), and grows a burst per underrun.
    const int32_t burst = AAudioStream_getFramesPerBurst(stream);
    if (burst > 0) {
        AAudioStream_setBufferSizeInFrames(stream, burst * 2);
    }
    m_lastXruns = AAudioStream_getXRunCount(stream);
    RT_LOGF(RT_TAG_AUDIO, "AAudio output: %d Hz, %s (asked %s), %s, burst %d, buffer %d of %d frames\n",
            AAudioStream_getSampleRate(stream), PerfModeName(AAudioStream_getPerformanceMode(stream)),
            PerfModeName(mode),
            AAudioStream_getSharingMode(stream) == AAUDIO_SHARING_MODE_EXCLUSIVE ? "exclusive" : "shared",
            AAudioStream_getFramesPerBurst(stream), AAudioStream_getBufferSizeInFrames(stream),
            AAudioStream_getBufferCapacityInFrames(stream));
    return true;
}

// Low latency first; some devices refuse the fast path at start (ARMSX2 saw Adreno phones fail the
// start with a disconnect), so retry once, then fall back to the shared slow path. A reopen keeps the
// mode that worked.
bool AudioBackend::OpenAAudioLocked() {
    const aaudio_performance_mode_t modes[] = {AAUDIO_PERFORMANCE_MODE_LOW_LATENCY, AAUDIO_PERFORMANCE_MODE_NONE};
    for (const aaudio_performance_mode_t mode : modes) {
        if (mode == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY && m_aaudioMode != mode) {
            continue;
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (TryOpenAAudioLocked(mode)) {
                m_aaudioMode = mode;
                return true;
            }
            SDL_Delay(60);
        }
    }
    RT_LOG(RT_TAG_AUDIO) << "AAudio: every open failed, audio is silent" << std::endl;
    return false;
}

void AudioBackend::CloseAAudioLocked() {
    if (m_aaudio != nullptr) {
        AAudioStream_requestStop(m_aaudio);
        AAudioStream_close(m_aaudio);
        m_aaudio = nullptr;
    }
}
#endif

bool AudioBackend::OpenOutputLocked() {
#if defined(__ANDROID__)
    if (m_useAAudio) {
        if (OpenAAudioLocked()) {
            return true;
        }
        RT_LOG(RT_TAG_AUDIO) << "AAudio unusable: falling back to SDL audio" << std::endl;
        m_useAAudio = false;
    }
#endif
    m_stream = OpenStreamLocked(m_spec);
    return m_stream != nullptr;
}

void AudioBackend::CloseOutputLocked() {
#if defined(__ANDROID__)
    CloseAAudioLocked();
#endif
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
}

// The app leaving the screen pauses the output, so a backgrounded game does not keep the device
// playing silence (and awake); coming back restarts it, or reopens it when the system took the
// device away in the meantime (ARMSX2 issues #333/#825).
void AudioBackend::SetBackgroundLocked(bool background) {
    if (m_background == background) {
        return;
    }
    m_background = background;
#if defined(__ANDROID__)
    if (!m_useAAudio) {
        return;
    }
    if (background) {
        if (m_aaudio != nullptr && AAudioStream_requestPause(m_aaudio) != AAUDIO_OK) {
            AAudioStream_requestStop(m_aaudio);
        }
        return;
    }
    m_lastPullMs.store(SDL_GetTicks(), std::memory_order_relaxed);
    if (m_aaudio == nullptr || m_aaudioLost.load(std::memory_order_acquire) ||
        AAudioStream_requestStart(m_aaudio) != AAUDIO_OK) {
        RT_LOG(RT_TAG_AUDIO) << "AAudio: restart after background failed, reopening" << std::endl;
        CloseAAudioLocked();
        OpenAAudioLocked();
    }
#endif
}

bool SDLCALL AudioBackend::LifecycleWatch(void* userdata, SDL_Event* event) {
    auto* self = static_cast<AudioBackend*>(userdata);
    if (event->type == SDL_EVENT_DID_ENTER_BACKGROUND || event->type == SDL_EVENT_WILL_ENTER_FOREGROUND) {
        std::lock_guard<std::mutex> lock(self->m_mutex);
        if (self->m_initialized) {
            self->SetBackgroundLocked(event->type == SDL_EVENT_DID_ENTER_BACKGROUND);
        }
    }
    return true;
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

    CloseOutputLocked();

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
#if defined(__ANDROID__)
    m_useAAudio = m_stretch && DirectAAudioWanted();
    // Recording-friendly (ARMSX2): low latency can take the MMAP path, which writes past the mixer a
    // screen recording taps, so the video is silent with no error anywhere. None stays on the mixer.
    if (const char* rec = std::getenv("BASARA_AUDIO_RECORDABLE"); rec != nullptr && std::strcmp(rec, "1") == 0) {
        m_aaudioMode = AAUDIO_PERFORMANCE_MODE_NONE;
        RT_LOG(RT_TAG_AUDIO) << "recording-friendly audio: performance mode none" << std::endl;
    }
#endif
    RT_LOGF(RT_TAG_AUDIO, "output %u Hz, %s, %s\n", sampleRate,
            m_stretch ? "time stretching on" : "time stretching off (BASARA_AUDIO_STRETCH=0)",
            m_useAAudio ? "AAudio direct" : "SDL audio");

    m_spec = spec;
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_gain.store(EffectiveGainLocked(), std::memory_order_relaxed);
    if (!OpenOutputLocked()) {
        return false;
    }
    m_initialized = true;
    return true;
}

bool AudioBackend::Init(uint32_t sampleRate, uint32_t channels) {
    // Added outside m_mutex: SDL calls the watch under its own lock, and the watch takes m_mutex.
    if (!m_watching) {
        m_watching = SDL_AddEventWatch(&AudioBackend::LifecycleWatch, this);
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return EnsureInitializedLocked(sampleRate, channels);
}

void AudioBackend::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    CloseOutputLocked();
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
    if (m_background) {
        return;  // paused on purpose: nothing pulls
    }
    const uint64_t now = SDL_GetTicks();
#if defined(__ANDROID__)
    if (m_useAAudio && m_aaudioLost.load(std::memory_order_acquire) && now - m_lastReopenMs >= 500) {
        m_lastReopenMs = now;
        ++m_statReopens;
        RT_LOG(RT_TAG_AUDIO) << "AAudio output disconnected: reopening" << std::endl;
        CloseAAudioLocked();
        OpenAAudioLocked();
        return;
    }
#endif
    // The audio thread can store a pull time later than `now`: never subtract past zero.
    const uint64_t lastPull = m_lastPullMs.load(std::memory_order_relaxed);
    if (now <= lastPull || now - lastPull < kStalledPullMs || now - m_lastReopenMs < kReopenIntervalMs) {
        return;
    }
    m_lastReopenMs = now;
    ++m_statReopens;
    RT_LOGF(RT_TAG_AUDIO, "output stalled (no pull for %llu ms): reopening the audio device\n",
            static_cast<unsigned long long>(now - lastPull));
    CloseOutputLocked();
    OpenOutputLocked();
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
#if defined(__ANDROID__)
    if (m_aaudio != nullptr) {
        const int32_t xruns = AAudioStream_getXRunCount(m_aaudio);
        stats.xruns = xruns > m_lastXruns ? static_cast<uint32_t>(xruns - m_lastXruns) : 0;
        m_lastXruns = xruns;
        if (stats.xruns > 0) {
            const int32_t burst = AAudioStream_getFramesPerBurst(m_aaudio);
            const int32_t size = AAudioStream_getBufferSizeInFrames(m_aaudio);
            const int32_t grown = AAudioStream_setBufferSizeInFrames(m_aaudio, size + std::max(burst, 1));
            if (grown > size) {
                RT_LOGF(RT_TAG_AUDIO, "AAudio: %u underruns, device buffer %d -> %d frames\n", stats.xruns, size,
                        grown);
            }
        }
    }
#endif
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
