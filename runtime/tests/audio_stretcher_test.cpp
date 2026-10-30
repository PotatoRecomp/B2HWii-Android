// AudioStretcher check: a 440 Hz sine pushed in bursts at a given fraction of real time, pulled in
// device-sized chunks. Reports output gaps (runs of silence), the largest sample-to-sample jump
// (a click shows up as a jump far above the sine's own slope) and the tempo range.
//   clang++ -std=c++17 -O2 -Iruntime/include runtime/tests/audio_stretcher_test.cpp
//           runtime/src/audio_stretcher.cpp -o audio_stretcher_test.exe
#include "audio_stretcher.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int Run(double speed, int burstFrames, int seconds) {
    constexpr uint32_t kRate = 32000;
    constexpr int kPull = 256;  // device period
    AudioStretcher stretcher;
    stretcher.Reset(kRate);
    double phase = 0.0, owed = 0.0;
    int16_t last[2] = {0, 0};
    int maxJump = 0, silentRun = 0, gaps = 0;
    bool sounding = false;
    float minTempo = 99.0f, maxTempo = 0.0f;
    std::vector<int16_t> burst, out(kPull * 2);
    for (long pulled = 0; pulled < static_cast<long>(kRate) * seconds; pulled += kPull) {
        // The game delivers speed * real time, a burst at a time.
        owed += speed * kPull;
        while (owed >= burstFrames) {
            burst.resize(static_cast<size_t>(burstFrames) * 2);
            for (int i = 0; i < burstFrames; ++i) {
                const auto s = static_cast<int16_t>(std::lround(12000.0 * std::sin(phase)));
                phase += 2.0 * 3.14159265358979 * 440.0 / kRate;
                burst[i * 2] = burst[i * 2 + 1] = s;
            }
            stretcher.Push(burst.data(), static_cast<size_t>(burstFrames));
            owed -= burstFrames;
        }
        stretcher.Pull(out.data(), kPull);
        for (int i = 0; i < kPull; ++i) {
            const int16_t s = out[i * 2];
            if (s == 0 && out[i * 2 + 1] == 0) {
                if (++silentRun == 64 && sounding) {
                    ++gaps;
                    sounding = false;
                }
            } else {
                silentRun = 0;
                sounding = true;
            }
            maxJump = std::max(maxJump, std::abs(s - last[0]));
            last[0] = s;
        }
        if (pulled % kRate == 0) {
            const AudioStretcher::Stats st = stretcher.TakeStats();
            if (st.maxTempo > 0.0f) {
                minTempo = std::min(minTempo, st.minTempo);
                maxTempo = std::max(maxTempo, st.maxTempo);
            }
        }
    }
    // The sine's own largest step is 12000 * 2*pi*440/32000 ~ 1037.
    std::printf("speed %.2f burst %d: gaps %d, max jump %d, tempo %.2f-%.2f\n", speed, burstFrames, gaps, maxJump,
                minTempo, maxTempo);
    return 0;
}

} // namespace

int main() {
    Run(1.0, 533, 20);   // full speed, one VI frame of audio a burst
    Run(1.0, 1600, 20);  // full speed, 3 frames a burst
    Run(1.0, 2400, 20);  // full-rate audio from a 20 fps game: 75 ms lumps
    Run(0.5, 533, 20);
    Run(0.33, 1066, 20);
    Run(0.2, 1600, 20);
    Run(1.6, 533, 20);   // catching up
    return 0;
}
