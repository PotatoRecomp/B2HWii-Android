#include "audio_stretcher.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kMinTempo = 0.15f;
constexpr float kMaxTempo = 2.5f;
// Per 20 ms segment: about 100 ms to follow a change in the game's speed. The guest pushes in
// VI-batched bursts, so the backlog swings by a frame's worth of audio even at full speed; the dead
// band keeps those swings at tempo 1 (no splices at all).
constexpr float kTempoSmoothing = 0.2f;
// The game's audio rate (input per real time), averaged over about 400 ms of bursts.
constexpr float kRateSmoothing = 0.05f;
constexpr float kBurstDecay = 0.995f;
constexpr float kTempoDeadBand = 0.12f;

int16_t ToSample(float value) {
    return static_cast<int16_t>(std::clamp(std::lround(value), -32768L, 32767L));
}

} // namespace

void AudioStretcher::Reset(uint32_t sampleRate) {
    m_rate = sampleRate;
    m_hop = std::max<int64_t>(1, sampleRate * 20 / 1000);
    m_overlap = std::max<int64_t>(1, sampleRate * 6 / 1000);
    m_search = std::max<int64_t>(1, sampleRate * 8 / 1000);
    // A segment reads up to m_search past the analysis position, and the overlap after it stays in the
    // buffer so running dry can always fade out instead of cutting off.
    m_need = m_search + m_hop + m_overlap;
    m_target = sampleRate * 40 / 1000;
    m_maxBacklog = sampleRate / 2;
    m_in.clear();
    m_nominal = 0.0;
    m_cont = 0;
    m_playing = false;
    m_tempo = 1.0f;
    m_inputRate = 1.0f;
    m_pushedFrames = 0;
    m_burstPeak = 0.0f;
    m_pullPeak = 0.0f;
    m_draining = false;
    m_out.clear();
    m_outPos = 0;
    m_stats = {};
    m_statsFresh = true;
}

void AudioStretcher::Push(const int16_t* frames, size_t frameCount) {
    m_in.insert(m_in.end(), frames, frames + frameCount * 2);
    m_pushedFrames += static_cast<int64_t>(frameCount);
}

float AudioStretcher::Mono(int64_t frame) const {
    return static_cast<float>(m_in[frame * 2]) + static_cast<float>(m_in[frame * 2 + 1]);
}

// The start in [lo, hi] whose first m_overlap frames best continue the last segment (normalized
// cross-correlation with the frames that would have followed it).
int64_t AudioStretcher::FindSplice(int64_t lo, int64_t hi) const {
    const int64_t span = hi - lo + m_overlap;
    std::vector<float> mono(static_cast<size_t>(span));
    for (int64_t i = 0; i < span; ++i) {
        mono[i] = Mono(lo + i);
    }
    std::vector<float> ref(static_cast<size_t>(m_overlap));
    for (int64_t i = 0; i < m_overlap; ++i) {
        ref[i] = Mono(m_cont + i);
    }
    float energy = 0.0f;
    for (int64_t i = 0; i < m_overlap; ++i) {
        energy += mono[i] * mono[i];
    }
    int64_t best = m_cont >= lo && m_cont <= hi ? m_cont : lo;
    float bestScore = -1e30f;
    for (int64_t c = 0; c <= hi - lo; ++c) {
        if (c > 0) {
            const float out = mono[c - 1], in = mono[c + m_overlap - 1];
            energy = std::max(0.0f, energy - out * out + in * in);
        }
        float dot = 0.0f;
        for (int64_t i = 0; i < m_overlap; ++i) {
            dot += ref[i] * mono[c + i];
        }
        const float score = dot / std::sqrt(energy + 1.0f);
        if (score > bestScore) {
            bestScore = score;
            best = lo + c;
        }
    }
    return best;
}

void AudioStretcher::SilentSegment() {
    const size_t start = m_out.size();
    m_out.resize(start + static_cast<size_t>(m_hop) * 2, 0);
    const int64_t inFrames = static_cast<int64_t>(m_in.size() / 2);
    if (m_playing && m_cont + m_overlap <= inFrames) {
        // Fade out what would have come next instead of cutting the waveform off.
        for (int64_t i = 0; i < m_overlap; ++i) {
            const float w = 1.0f - (static_cast<float>(i) + 0.5f) / static_cast<float>(m_overlap);
            m_out[start + i * 2] = ToSample(m_in[(m_cont + i) * 2] * w);
            m_out[start + i * 2 + 1] = ToSample(m_in[(m_cont + i) * 2 + 1] * w);
        }
    }
    if (m_playing) {
        ++m_stats.gaps;
        m_nominal = static_cast<double>(std::min(m_cont, inFrames));  // resume where it stopped
    }
    m_playing = false;
}

bool AudioStretcher::MakeSegment() {
    const int64_t inFrames = static_cast<int64_t>(m_in.size() / 2);
    int64_t backlog = inFrames - static_cast<int64_t>(m_nominal);
    bool resync = false;
    if (backlog > m_maxBacklog) {
        m_nominal = static_cast<double>(inFrames - m_need - m_target);
        backlog = m_need + m_target;
        resync = true;
        ++m_stats.skips;
    }
    const float backlogMs = static_cast<float>(backlog) * 1000.0f / static_cast<float>(m_rate);
    m_stats.minBacklogMs = m_stats.minBacklogMs < 0.0f ? backlogMs : std::min(m_stats.minBacklogMs, backlogMs);

    m_inputRate += (static_cast<float>(m_pushedFrames) / static_cast<float>(m_hop) - m_inputRate) * kRateSmoothing;
    // The largest burst lately (decays over a few seconds): at 30 fps a G99 delivers full-rate audio
    // in lumps big enough to drain a 40 ms cushion between them.
    m_burstPeak = std::max(static_cast<float>(m_pushedFrames), m_burstPeak * kBurstDecay);
    m_pushedFrames = 0;
    // Between two bursts the backlog drains by about one burst whatever the speed, and a slow game
    // pushes bigger ones (several frames of audio at once): hold a deeper cushion when it runs slow.
    // 40 ms at full speed (less delay behind the picture), the old 60 once the game runs slow.
    const float base = m_inputRate < 1.0f - kTempoDeadBand ? static_cast<float>(m_rate) * 0.06f
                                                           : static_cast<float>(m_target);
    const int64_t cushion = std::min<int64_t>(
        m_maxBacklog / 2,
        static_cast<int64_t>(std::max(base *
                                          std::clamp(1.0f + (1.0f - m_inputRate) * 1.5f, 1.0f, 2.0f),
                                      std::max(m_burstPeak, m_pullPeak) * 1.25f)));

    if (!m_playing) {
        // Start (or restart after running dry) only with a full backlog, so a slow game does not
        // flicker between sound and silence.
        if (backlog < m_need + cushion) {
            SilentSegment();
            return false;
        }
        const int64_t c = static_cast<int64_t>(m_nominal);
        const size_t start = m_out.size();
        m_out.resize(start + static_cast<size_t>(m_hop) * 2);
        for (int64_t i = 0; i < m_hop; ++i) {
            const float w = i < m_overlap ? (static_cast<float>(i) + 0.5f) / static_cast<float>(m_overlap) : 1.0f;
            m_out[start + i * 2] = ToSample(m_in[(c + i) * 2] * w);
            m_out[start + i * 2 + 1] = ToSample(m_in[(c + i) * 2 + 1] * w);
        }
        m_cont = c + m_hop;
        m_nominal = static_cast<double>(m_cont);
        m_playing = true;  // m_tempo kept: a slow game restarts at its own pace
        return true;
    }

    // Follow the game's rate, corrected toward a full cushion.
    const float rawFill = static_cast<float>(backlog - m_need) / static_cast<float>(cushion);
    const float fill = std::clamp(rawFill, 0.5f, 2.0f);
    // At full speed the bursts alone swing the fill by half a cushion or more (a movie on a G81 decodes
    // a picture every other frame): leave those at tempo 1, the stretching was audible as a warble.
    // A backlog past 1.4 cushions (left over from a load) is drained back to about one, instead of
    // sitting there with the voice late behind the picture.
    if (rawFill >= 1.4f) {
        m_draining = true;
    } else if (rawFill <= 1.05f) {
        m_draining = false;
    }
    const bool fullSpeed = std::fabs(m_inputRate - 1.0f) < kTempoDeadBand && rawFill > 0.3f && !m_draining;
    const float raw = fullSpeed ? 1.0f : std::clamp(m_inputRate * fill, kMinTempo, kMaxTempo);
    m_tempo += (raw - m_tempo) * kTempoSmoothing;
    const float tempo = std::fabs(m_tempo - 1.0f) < kTempoDeadBand ? 1.0f : m_tempo;
    if (m_statsFresh) {
        m_stats.minTempo = m_stats.maxTempo = tempo;
        m_statsFresh = false;
    }
    m_stats.minTempo = std::min(m_stats.minTempo, tempo);
    m_stats.maxTempo = std::max(m_stats.maxTempo, tempo);

    const size_t start = m_out.size();
    if (tempo == 1.0f && !resync) {
        // Full speed: carry straight on, sample for sample.
        if (m_cont + m_hop + m_overlap > inFrames) {
            SilentSegment();
            return false;
        }
        m_out.insert(m_out.end(), m_in.begin() + m_cont * 2, m_in.begin() + (m_cont + m_hop) * 2);
        m_cont += m_hop;
        m_nominal = static_cast<double>(m_cont);
        return true;
    }

    const int64_t a = static_cast<int64_t>(m_nominal);
    const int64_t lo = std::max<int64_t>(0, a - m_search);
    const int64_t hi = a + m_search;
    if (hi + m_hop + m_overlap > inFrames) {
        SilentSegment();
        return false;
    }
    const int64_t c = FindSplice(lo, hi);
    m_out.resize(start + static_cast<size_t>(m_hop) * 2);
    for (int64_t i = 0; i < m_hop; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            float value = m_in[(c + i) * 2 + ch];
            if (i < m_overlap) {
                const float w = (static_cast<float>(i) + 0.5f) / static_cast<float>(m_overlap);
                value = m_in[(m_cont + i) * 2 + ch] * (1.0f - w) + value * w;
            }
            m_out[start + i * 2 + ch] = ToSample(value);
        }
    }
    m_cont = c + m_hop;
    m_nominal += static_cast<double>(tempo) * static_cast<double>(m_hop);
    return true;
}

void AudioStretcher::Compact() {
    const int64_t keepFrom = std::min(m_cont, static_cast<int64_t>(m_nominal) - m_search);
    if (keepFrom <= static_cast<int64_t>(m_rate / 4)) {
        return;
    }
    m_in.erase(m_in.begin(), m_in.begin() + keepFrom * 2);
    m_cont -= keepFrom;
    m_nominal -= static_cast<double>(keepFrom);
}

void AudioStretcher::Pull(int16_t* out, size_t frameCount) {
    if (m_rate == 0) {
        std::fill(out, out + frameCount * 2, int16_t{0});
        return;
    }
    // A device on the shared (non low-latency) path pulls a large burst at once (1365 frames = 43 ms
    // at 32 kHz on a Helio G85 with Recording-Friendly Audio). A cushion smaller than one pull runs
    // dry inside it and restarts from silence every few pulls: crackle. Size the cushion to it.
    m_pullPeak = std::max(static_cast<float>(frameCount), m_pullPeak * kBurstDecay);
    while ((m_out.size() - m_outPos) / 2 < frameCount) {
        MakeSegment();
        Compact();
    }
    std::copy(m_out.begin() + m_outPos, m_out.begin() + m_outPos + frameCount * 2, out);
    m_outPos += frameCount * 2;
    if (m_outPos * 2 > m_out.size()) {
        m_out.erase(m_out.begin(), m_out.begin() + m_outPos);
        m_outPos = 0;
    }
    m_stats.playedMs += static_cast<float>(frameCount) * 1000.0f / static_cast<float>(m_rate);
}

AudioStretcher::Stats AudioStretcher::TakeStats() {
    Stats stats = m_stats;
    m_stats = {};
    m_statsFresh = true;
    return stats;
}
