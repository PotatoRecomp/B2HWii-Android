#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Time stretching for the host output (WSOLA, stereo int16): the device pulls audio at its own rate,
// the game pushes it at the rate it runs. When the game runs slower than real time the output tempo
// follows it down instead of running dry between blocks (the crackle on slow phones); when it catches
// up in a burst the tempo rises instead of dropping blocks. At full speed the tempo is exactly 1 and
// the samples pass through untouched. Not thread-safe: the caller locks.
class AudioStretcher {
public:
    void Reset(uint32_t sampleRate);
    void Push(const int16_t* frames, size_t frameCount);
    // Writes exactly frameCount stereo frames (silence while there is too little input).
    void Pull(int16_t* out, size_t frameCount);

    struct Stats {
        uint32_t gaps = 0;   // segments played as silence because input ran out
        uint32_t skips = 0;  // jumps forward over a backlog too long to catch up on
        float minBacklogMs = -1.0f;
        float minTempo = 0.0f;
        float maxTempo = 0.0f;
        float playedMs = 0.0f;
    };
    Stats TakeStats();

private:
    bool MakeSegment();
    void SilentSegment();
    void Compact();
    float Mono(int64_t frame) const;
    int64_t FindSplice(int64_t lo, int64_t hi) const;

    uint32_t m_rate = 0;
    int64_t m_hop = 0;      // output frames per segment
    int64_t m_overlap = 0;  // crossfade frames
    int64_t m_search = 0;   // splice search radius, frames
    int64_t m_need = 0;     // input past the analysis position needed for one segment
    int64_t m_target = 0;   // backlog (beyond m_need) held at tempo 1
    int64_t m_maxBacklog = 0;

    std::vector<int16_t> m_in;  // interleaved stereo, frame 0 = oldest kept
    double m_nominal = 0.0;     // analysis position
    int64_t m_cont = 0;         // natural continuation of the last segment
    bool m_playing = false;
    float m_tempo = 1.0f;
    float m_inputRate = 1.0f;  // input frames per output frame
    int64_t m_pushedFrames = 0;  // since the last segment
    bool m_draining = false;     // a backlog left over from a load being played off at tempo > 1
    float m_burstPeak = 0.0f;    // largest input between two segments lately, frames

    std::vector<int16_t> m_out;
    size_t m_outPos = 0;

    Stats m_stats;
    bool m_statsFresh = true;
};
