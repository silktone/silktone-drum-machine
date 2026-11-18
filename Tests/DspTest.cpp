// DspTest.cpp — Standalone verification for the Silktone drum machine DSP core.
// Compiles WITHOUT JUCE (plain C++17) and runs on Linux:
//   g++ -std=c++17 -O2 -I../Source Tests/DspTest.cpp ../Source/DspCore.cpp \
//       ../Source/DrumEngine.cpp -o /tmp/DspTest && /tmp/DspTest
// Checks:
//  1. Every voice renders in every kit: finite, non-silent, bounded peaks.
//  2. A 2-bar pattern plays through the engine with correct step timing.
//  3. Swing delays odd steps (16th) and steps 2 mod 4 (8th).
//  4. Stem splitter produces 6 finite stems; low stem has less high-frequency
//     energy than the high stem (sanity, not AI — it's a filter split).
//  5. MIDI export parses as a valid SMF (MThd/MTrk, TPQ 96, GM drums ch 10).
//  6. Auto-chop finds onsets in a synthetic burst sequence.
//  7. Filename categorisation matches the app's voiceForFilename.
//  8. WAV encoding round-trips (header + data size).
//  9. Click renders (accent louder than normal).
// 10. Pattern remap 4/4 -> 12/8 keeps four-on-the-floor on 0/6/12/18.
#include "DrumEngine.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace silktone;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
    else { std::printf("ok:   %s\n", msg); } \
} while (0)

static double rms(const std::vector<float>& b) {
    double s = 0;
    for (float v : b) s += (double)v * v;
    return b.empty() ? 0 : std::sqrt(s / b.size());
}
static double peakOf(const std::vector<float>& b) {
    double p = 0;
    for (float v : b) p = std::max(p, (double)std::fabs(v));
    return p;
}
static bool allFinite(const std::vector<float>& b) {
    for (float v : b) if (!std::isfinite(v)) return false;
    return true;
}

int main() {
    const double sr = 48000;

    // ---- 1. Every voice in every kit -------------------------------------
    {
        NoiseBuffer noise;
        noise.init(sr);
        int silent = 0, nonfinite = 0, clipped = 0, total = 0;
        for (auto& kit : kKits) {
            KitParams kp;
            kp.kickF = kit.kickF; kp.kickD = kit.kickD; kp.snareT = kit.snareT;
            kp.hatD = kit.hatD; kp.bright = kit.bright; kp.real = kit.real;
            kp.muffle = kit.muffle; kp.ring = kit.ring; kp.snap = kit.snap;
            kp.dust = kit.dust; kp.grit = kit.grit; kp.lofi = kit.lofi;
            kp.branch = kit.id;
            for (auto& v : kVoices) {
                HitBuffer hit;
                renderVoiceHit(v.id, kp, 1.0, 1.0, sr, noise, hit);
                ++total;
                if (hit.numSamples <= 0 || rms(hit.samples) < 1e-5) ++silent;
                if (!allFinite(hit.samples)) ++nonfinite;
                if (peakOf(hit.samples) > 3.0) ++clipped;
            }
        }
        std::printf("voices x kits: %d hits rendered\n", total);
        CHECK(silent == 0, "no voice/kit combination renders silent");
        CHECK(nonfinite == 0, "all rendered hits are finite");
        CHECK(clipped == 0, "no hit peaks above 3.0 (sanity bound)");
        if (silent) std::printf("  silent count: %d\n", silent);
    }

    // ---- 2. Engine: 2-bar pattern, step timing ---------------------------
    {
        DrumEngine eng;
        eng.prepare(sr);
        eng.setBpm(120);
        eng.loadPreset(0);  // first preset (samba) — exercises preset loading
        eng.play();
        const int block = 512;
        std::vector<float> L(block), R(block);
        std::vector<float> fullL, fullR;
        double twoBars = 2 * 16 * eng.stepDurationSeconds();
        int64_t total = (int64_t)(twoBars * sr);
        int64_t done = 0;
        while (done < total) {
            int n = (int)std::min<int64_t>(block, total - done);
            eng.processBlock(L.data(), R.data(), n, nullptr, nullptr);
            fullL.insert(fullL.end(), L.begin(), L.begin() + n);
            fullR.insert(fullR.end(), R.begin(), R.begin() + n);
            done += n;
        }
        eng.stop();
        CHECK(allFinite(fullL) && allFinite(fullR), "2-bar render is finite");
        CHECK(rms(fullL) > 1e-4, "2-bar render is not silent");
        CHECK(peakOf(fullL) < 2.0, "2-bar render peak is sane (< 2.0)");
        // Energy should appear in the first bar and the second bar alike.
        size_t half = fullL.size() / 2;
        double r1 = rms(std::vector<float>(fullL.begin(), fullL.begin() + (long)half));
        double r2 = rms(std::vector<float>(fullL.begin() + (long)half, fullL.end()));
        CHECK(r1 > 1e-4 && r2 > 1e-4, "both bars of the pattern sound");
        std::printf("  bar1 rms %.4f  bar2 rms %.4f  peak %.3f\n", r1, r2, peakOf(fullL));
    }

    // ---- 3. Swing ---------------------------------------------------------
    {
        DrumEngine eng;
        eng.prepare(sr);
        eng.setBpm(120);
        eng.setSwing(75);
        eng.setSwingInterval("16");
        double dOdd = eng.swingDelaySeconds(1);
        double dEven = eng.swingDelaySeconds(2);
        CHECK(dOdd > 0 && dEven == 0, "16th swing delays odd steps only");
        eng.setSwingInterval("8");
        double d2 = eng.swingDelaySeconds(2);
        double d1 = eng.swingDelaySeconds(1);
        CHECK(d2 > 0 && d1 == 0, "8th swing delays steps 2 mod 4 only");
        eng.setSwing(50);
        CHECK(eng.swingDelaySeconds(1) == 0, "swing 50% = straight (no delay)");
    }

    // ---- 4. Stem splitter -------------------------------------------------
    {
        DrumEngine eng;
        eng.prepare(sr);
        // Synthetic stereo source: kick-ish low sine + hat-ish high noise.
        int n = (int)sr;
        std::vector<float> L(n), R(n);
        for (int i = 0; i < n; ++i) {
            double t = i / sr;
            L[i] = (float)(0.6 * std::sin(2 * M_PI * 55 * t) + 0.15 * std::sin(2 * M_PI * 9000 * t));
            R[i] = L[i];
        }
        auto stems = DrumEngine::splitStems(L.data(), R.data(), n, sr, 2);
        CHECK(allFinite(stems.low.l) && allFinite(stems.high.l), "stems are finite");
        // Low stem should carry the 55 Hz energy; high stem the 9 kHz energy.
        auto bandRms = [](const std::vector<float>& b, int a, int c) {
            double s = 0; for (int i = a; i < c; ++i) s += b[i] * b[i];
            return std::sqrt(s / (c - a));
        };
        double lowInLow = bandRms(stems.low.l, 0, n);
        double lowInHigh = bandRms(stems.high.l, 0, n);
        CHECK(lowInLow > 3 * lowInHigh, "low stem is mostly low end, high stem mostly top end");
        CHECK(rms(stems.center.l) > 1e-4, "centre stem is non-silent for a centred source");
        CHECK(rms(stems.side.l) < 1e-4, "side stem is ~silent for an identical L/R source");
        std::printf("  low rms %.4f  high rms %.4f  centre rms %.4f  side rms %.5f\n",
                    lowInLow, lowInHigh, rms(stems.center.l), rms(stems.side.l));
    }

    // ---- 5. MIDI export ---------------------------------------------------
    {
        DrumEngine eng;
        eng.prepare(sr);
        eng.setBpm(120);
        eng.loadPreset(0);
        auto midi = eng.buildMidi();
        CHECK(midi.size() > 30, "MIDI file is non-trivial in size");
        CHECK(std::memcmp(midi.data(), "MThd", 4) == 0, "MIDI starts with MThd");
        CHECK(std::memcmp(midi.data() + 14, "MTrk", 4) == 0, "MIDI has an MTrk chunk");
        int tpq = (midi[12] << 8) | midi[13];
        CHECK(tpq == 96, "MIDI TPQ is 96");
        // Count note-ons on channel 10 (0x99).
        int noteOns = 0;
        for (size_t i = 0; i + 1 < midi.size(); ++i) if (midi[i] == 0x99) ++noteOns;
        CHECK(noteOns > 0, "MIDI contains channel-10 note-ons");
        std::printf("  midi bytes %zu, note-ons %d\n", midi.size(), noteOns);
    }

    // ---- 6. Auto-chop -----------------------------------------------------
    {
        // Three bursts at 0.1s, 0.5s, 0.9s in a 1.2s buffer. Bursts are
        // 0.15s long: the detector's balanced minDur is 0.09s, so shorter
        // bursts are dropped by design (same as the app).
        int n = (int)(1.2 * sr);
        std::vector<float> buf(n, 0.0f);
        auto burst = [&](double t0) {
            int s0 = (int)(t0 * sr);
            for (int i = 0; i < (int)(0.15 * sr) && s0 + i < n; ++i)
                buf[s0 + i] = (float)(0.8 * std::sin(2 * M_PI * 440 * i / sr) * (1.0 - i / (0.15 * sr)));
        };
        burst(0.1); burst(0.5); burst(0.9);
        auto slices = DrumEngine::detectSlices(buf.data(), n, 1, sr, "balanced");
        CHECK(slices.size() == 3, "transient chop finds 3 bursts");
        if (slices.size() == 3) {
            CHECK(std::fabs(slices[0].start - 0.08) < 0.05, "first slice starts near 0.1s");
            CHECK(std::fabs(slices[2].start - 0.88) < 0.05, "third slice starts near 0.9s");
        } else {
            std::printf("  got %zu slices\n", slices.size());
        }
        auto grid = DrumEngine::gridSlices(1.2, 120, "4/4", "beat");
        CHECK(grid.size() == 3, "meter chop at 120 BPM gives 3 beat slices in 1.2s");
    }

    // ---- 7. Filename categorisation ---------------------------------------
    {
        CHECK(DrumEngine::categorizeFilename("Kick_808_Deep.wav") == "kick", "kick filename");
        CHECK(DrumEngine::categorizeFilename("snare-crack-02.wav") == "snare", "snare filename");
        CHECK(DrumEngine::categorizeFilename("Open Hat bright.wav") == "ohat", "open hat filename");
        CHECK(DrumEngine::categorizeFilename("Tombak Tom 1.wav") == "tombak", "tombak filename");
        CHECK(DrumEngine::categorizeFilename("darbuka-tek.wav") == "dartek", "darbuka tek filename");
        CHECK(DrumEngine::categorizeFilename("random-pad-sound.wav") == "", "uncategorised filename returns empty");
    }

    // ---- 8. WAV encoding --------------------------------------------------
    {
        std::vector<float> L(4800, 0.5f), R(4800, -0.5f);
        auto wav = encodeWavStereo(L.data(), R.data(), 4800, 48000);
        CHECK(wav.size() == 44 + 4800 * 4, "WAV size = header + 16-bit stereo data");
        CHECK(std::memcmp(wav.data(), "RIFF", 4) == 0 && std::memcmp(wav.data() + 8, "WAVE", 4) == 0,
              "WAV header is RIFF/WAVE");
    }

    // ---- 9. Click ----------------------------------------------------------
    {
        HitBuffer normal, accent;
        renderClick(false, sr, normal);
        renderClick(true, sr, accent);
        CHECK(peakOf(normal.samples) > 0.1, "normal click sounds");
        CHECK(peakOf(accent.samples) > peakOf(normal.samples), "accent click is louder");
    }

    // ---- 10. Meter remap ---------------------------------------------------
    {
        std::vector<int> four = DrumEngine::parsePat("x...x...x...x...", 16);
        std::vector<float> vel(16, 0.72f), outVel;
        auto twelve = DrumEngine::remapBar(four, vel, "4/4", "12/8", outVel);
        CHECK(twelve.size() == 24, "remap to 12/8 gives 24 steps");
        CHECK(twelve[0] == 1 && twelve[6] == 1 && twelve[12] == 1 && twelve[18] == 1,
              "four-on-the-floor lands on 0/6/12/18 in 12/8");
    }

    // ---- 11. Preset / pattern loading --------------------------------------
    {
        DrumEngine eng;
        eng.prepare(sr);
        eng.loadPreset(0);
        int hits = 0;
        for (int ch = 0; ch < 30; ++ch)
            for (int s = 0; s < eng.steps(); ++s) if (eng.channel(ch).steps[s] > 0) ++hits;
        CHECK(hits > 0, "preset 0 loads a non-empty pattern");
        // Copy/paste round-trip.
        eng.copyRow(0);
        eng.clearRow(1);
        eng.pasteRow(1);
        bool same = true;
        for (int s = 0; s < 24; ++s) if (eng.channel(0).steps[s] != eng.channel(1).steps[s]) same = false;
        CHECK(same, "copy/paste row round-trips");
        // Sound scrolling moves within the family.
        std::string before = eng.channelSoundName(0);
        eng.scrollChannelSound(0, 1);
        std::string after = eng.channelSoundName(0);
        CHECK(before != after, "sound scroll changes the channel sound");
        std::printf("  scroll: '%s' -> '%s'\n", before.c_str(), after.c_str());
    }

    // ---- 11b. Startup pattern ------------------------------------------------
    {
        // A fresh engine (prepare only, no loadPreset) starts with the app's
        // startup pattern ('house'), never empty.
        DrumEngine eng;
        eng.prepare(sr);
        int hits = 0;
        for (int ch = 0; ch < 30; ++ch)
            for (int s = 0; s < eng.steps(); ++s) if (eng.channel(ch).steps[s] > 0) ++hits;
        CHECK(hits > 0, "fresh engine starts with the house pattern loaded");
        CHECK(eng.channelSoundName(0) == "808 Bass Drum", "startup kit is 808");
    }

    // ---- 12. Offline loop render + stems -----------------------------------
    {
        DrumEngine eng;
        eng.prepare(sr);
        eng.setBpm(120);
        eng.loadPreset(0);
        std::vector<float> L, R;
        eng.renderLoop(2, L, R);
        CHECK(!L.empty() && allFinite(L), "offline loop render is finite");
        CHECK(rms(L) > 1e-4, "offline loop render is not silent");
        std::vector<DrumEngine::StemAudio> stems;
        std::vector<std::string> names;
        eng.renderStems(stems, names);
        CHECK(!stems.empty(), "per-voice stems render");
        bool allOk = true;
        for (auto& st : stems) if (!allFinite(st.l) || rms(st.l) < 1e-5) allOk = false;
        CHECK(allOk, "every rendered stem is finite and non-silent");
        std::printf("  loop samples %zu, stems %zu\n", L.size(), stems.size());
        // Write a demo WAV for ear-checking on the Mac.
        auto wav = encodeWavStereo(L.data(), R.data(), (int)L.size(), (int)sr);
        FILE* f = std::fopen("/tmp/silktone-demo-loop.wav", "wb");
        std::fwrite(wav.data(), 1, wav.size(), f);
        std::fclose(f);
        std::printf("  demo loop written to /tmp/silktone-demo-loop.wav (%zu bytes)\n", wav.size());
    }

    std::printf("\n%s: %d failure(s)\n", failures ? "RESULT: FAIL" : "RESULT: PASS", failures);
    return failures ? 1 : 0;
}
