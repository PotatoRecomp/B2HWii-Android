#pragma once

#include "audio_stretcher.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>

#if defined(__ANDROID__)
#include <aaudio/AAudio.h>
#endif

class AudioBackend {
public:
    static AudioBackend& Instance();

    bool Init(uint32_t sampleRate, uint32_t channels);
    void Shutdown();

    // Wii AI DMA frames are big-endian and ordered right, left. SDL expects
    // native-endian interleaved left, right samples.
    bool PushWiiAiSamplesBE16(const uint8_t* data, size_t bytes);
    bool PushSamplesLE16(const int16_t* samples, size_t sampleCount);

    // Applied to the final host output, covering both AX and direct AI DMA.
    void SetMasterVolume(float volume);
    void SetMuted(bool muted);

    // Output health since the last call (the fps log): blocks pushed, pushes that found the output
    // queue empty (the device ran dry before them, an audible gap: crackle), blocks dropped on a full
    // queue, the lowest queue level seen and the audio pushed, both in ms. With time stretching (the
    // default) empty counts stretcher gaps, dropped its skips, and the queue is the stretcher backlog.
    struct Stats {
        bool stretching = false;
        uint32_t pushes = 0;
        uint32_t empty = 0;
        uint32_t dropped = 0;
        float minQueuedMs = -1.0f;
        float pushedMs = 0.0f;
        float playedMs = 0.0f;
        float minTempo = 0.0f;
        float maxTempo = 0.0f;
        uint32_t reopens = 0;
        uint32_t xruns = 0;  // device underruns (AAudio output only)
    };
    Stats TakeStats();

private:
    AudioBackend() = default;
    ~AudioBackend() = default;
    AudioBackend(const AudioBackend&) = delete;
    AudioBackend& operator=(const AudioBackend&) = delete;

    bool EnsureInitializedLocked(uint32_t sampleRate, uint32_t channels);
    SDL_AudioStream* OpenStreamLocked(const SDL_AudioSpec& spec);
    bool QueueHasCapacityLocked(int incomingBytes);
    uint32_t QueueLimitBytesLocked() const;
    float EffectiveGainLocked() const;
    void ApplyGainLocked();
    bool PushFramesLocked(const int16_t* frames, size_t frameCount);
    void ReopenIfStalledLocked();
    static void SDLCALL PullCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount);
    void PullStretched(int16_t* out, size_t frames);
    bool OpenOutputLocked();
    void CloseOutputLocked();
    static bool SDLCALL LifecycleWatch(void* userdata, SDL_Event* event);
    void SetBackgroundLocked(bool background);
#if defined(__ANDROID__)
    // Android with time stretching: the stretcher feeds AAudio straight from its data callback, the
    // way ARMSX2's Oboe output does (no SDL audio thread and triple buffer in between).
    bool OpenAAudioLocked();
    bool TryOpenAAudioLocked(aaudio_performance_mode_t mode);
    void CloseAAudioLocked();
    static aaudio_data_callback_result_t AAudioData(AAudioStream* stream, void* userdata, void* audioData,
                                                    int32_t numFrames);
    static void AAudioError(AAudioStream* stream, void* userdata, aaudio_result_t error);
    AAudioStream* m_aaudio = nullptr;
    aaudio_performance_mode_t m_aaudioMode = AAUDIO_PERFORMANCE_MODE_LOW_LATENCY;
    std::atomic<bool> m_aaudioLost{false};
    int32_t m_lastXruns = 0;
#endif
    bool m_useAAudio = false;
    bool m_background = false;
    bool m_watching = false;
    std::atomic<float> m_gain{1.0f};

    mutable std::mutex m_mutex;
    // Stretching: the stretcher, its stats and the pull time, shared with SDL's audio thread. Lock
    // order is m_mutex then m_stretchMutex, and no SDL stream call is made under m_stretchMutex.
    std::mutex m_stretchMutex;
    AudioStretcher m_stretcher;
    std::vector<int16_t> m_pullBuffer;
    std::atomic<uint64_t> m_lastPullMs{0};
    uint64_t m_lastReopenMs = 0;
    uint32_t m_statReopens = 0;
    bool m_stretch = true;
    SDL_AudioStream* m_stream = nullptr;
    SDL_AudioSpec m_spec{};
    uint32_t m_sampleRate = 0;
    uint32_t m_channels = 0;
    bool m_initialized = false;
    float m_masterVolume = 1.0f;
    bool m_muted = false;
    bool m_reportedDroppedBlock = false;
    std::vector<int16_t> m_convertBuffer;
    uint32_t m_statPushes = 0;
    uint32_t m_statEmpty = 0;
    uint32_t m_statDropped = 0;
    int m_statMinQueued = -1;
    uint64_t m_statPushedBytes = 0;
};
