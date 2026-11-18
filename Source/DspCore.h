// DspCore.h — Platform-independent DSP for the Silktone drum machine plugin.
// No JUCE dependency: plain C++17, so the same code can be unit-tested on
// Linux and runs inside the JUCE AU/VST3 wrapper on macOS.
//
// Faithful port of the voice synthesis in
// ~/workspace/your_files/drum-machine/drum-machine.html (playVoice,
// tone(), noise(), rings(), applySat, the per-kit branches).
// Web Audio node semantics are reproduced sample by sample:
//  - tone(): oscillator with exponential pitch glide over min(0.09, dec*0.5),
//    gain env = linear attack 0.004s to peak, exponential decay to 0.0001.
//  - noise(): white-noise burst through an RBJ biquad (lowpass/highpass/
//    bandpass, optional frequency glide), gain env attack 0.002s.
//  - Saturation waveshaper: (1+k)x / (1+k|x|), k = 1 + sat*30, trim applied
//    after, exactly as applySat() in the app.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace silktone {

// ---------------------------------------------------------------- utilities
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ------------------------------------------------------------------ biquad
// RBJ cookbook biquad, same coefficients Web Audio's BiquadFilterNode uses.
struct Biquad {
    enum Type { LowPass, HighPass, BandPass };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void set(Type type, double freqHz, double q, double sampleRate);
    void reset() { x1 = x2 = y1 = y2 = 0; }
    inline double process(double x) {
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
    // One-pole lowpass, used by the app's "dusty" branch in a few places.
    struct OnePole {
        double a = 0, z = 0;
        void set(double freqHz, double sampleRate);
        inline double process(double x) { z += a * (x - z); return z; }
    };
};

// ------------------------------------------------------------ noise buffer
// The app keeps one shared 1.5s white-noise buffer; every noise() call
// reads from a random offset and wraps. Same here.
struct NoiseBuffer {
    std::vector<float> data;  // mono, 1.5s at the engine sample rate
    uint32_t seed = 22222;
    void init(double sampleRate);
    float next();  // deterministic pseudo-random, wraps
};

// ------------------------------------------------------- envelope helpers
// App env(): linear ramp 0.0001 -> peak over `attack`, then exponential
// decay to 0.0001 over `decay`. Returns the gain at time t (seconds).
inline double envGain(double t, double attack, double peak, double decay) {
    if (t < 0) return 0.0;
    if (t < attack) {
        double f = attack > 0 ? t / attack : 1.0;
        return 0.0001 + (peak - 0.0001) * f;
    }
    double d = t - attack;
    if (d >= decay) return 0.0;
    return peak * std::pow(0.0001 / peak, d / decay);
}

// ------------------------------------------------------------ voice synth
struct KitParams {
    double kickF = 150, kickD = 0.4, snareT = 190, hatD = 1.0, bright = 1.0;
    double real = 0, muffle = 0, ring = 0, snap = 0, dust = 0, grit = 0, lofi = 0;
    // Branch selector = the kit id as in the app ("acoustic", "linndrum",
    // "dmx", "sp1200", "simmons", "phonk", "drill", "jersey", "logdrum",
    // "linn", "cr78", or any other id for the generic machine branch).
    // Kits with real > 0 take the real/dusty family branch regardless.
    const char* branch = "generic";
};

// One rendered hit: mono samples in `samples`, valid for `numSamples`.
struct HitBuffer {
    std::vector<float> samples;
    int numSamples = 0;
};

// Renders one synthesized drum hit into `out` (mono). Mirrors playVoice()
// in the app, including every kit-specific branch (acoustic/real kits,
// LinnDrum, DMX, SP-1200, Simmons, Phonk, Drill, Jersey Club, log drum,
// CR-78) and the extra hand-drum voices (tombak, daf, darbuka, ...).
// `tuneRatio` = pow(2, semitones/12) — scales every frequency and the
// sample-playback rate, exactly as the app's `ratio` does.
// `velocity` is 0..1 (the app multiplies by the channel volume separately).
void renderVoiceHit(const char* voiceId, const KitParams& kit, double velocity,
                    double tuneRatio, double sampleRate, NoiseBuffer& noise,
                    HitBuffer& out);

// Saturation waveshaper, exactly as applySat() in the app.
// sat is 0..1 (the knob is 0..100 in the UI).
inline double applySat(double x, double sat) {
    double k = 1.0 + sat * 30.0;
    double trim = 1.0 / (1.0 + sat * 0.45);
    return (1.0 + k) * x / (1.0 + k * std::fabs(x)) * trim;
}

// ------------------------------------------------------- click (metronome)
// App playClick(): square osc, 1960 Hz accent / 1240 Hz normal, 0.5/0.32 peak,
// 0.055s / 0.04s decay. Live guide only — never in exports.
void renderClick(bool accent, double sampleRate, HitBuffer& out);

// ------------------------------------------------------------ hall reverb
// The app convolves with a generated 1.8s noise-decay impulse
// (noise * (1 - t/T)^2.6, stereo). The JUCE wrapper uses dsp::Convolution
// with that exact IR; this small algorithmic hall is the platform-
// independent equivalent used by the offline renderer and the tests.
// Freeverb-style: 4 parallel combs + 2 allpasses, ~1.8s decay.
struct HallReverb {
    void prepare(double sampleRate, double decaySeconds = 1.8);
    void reset();
    void setWet(double wet) { wet_ = (float)clampd(wet, 0.0, 1.0); }
    // In-place stereo processing of one block.
    void process(float* left, float* right, int numSamples);
private:
    struct Comb { std::vector<float> buf; int idx = 0; float feedback = 0; float damp = 0; float dampZ = 0;
        float process(float x); };
    struct Allpass { std::vector<float> buf; int idx = 0; float feedback = 0.5f;
        float process(float x); };
    Comb combs_[4];
    Allpass allpasses_[2];
    float wet_ = 1.0f;
    double sampleRate_ = 48000;
};

// ---------------------------------------------------------- echo (delay)
// App: DelayNode, time = 3 steps (dotted eighth), feedback 0.32,
// lowpass in the loop at 3200 Hz.
struct EchoDelay {
    void prepare(double sampleRate);
    void reset();
    void setDelaySeconds(double seconds);
    void setFeedback(double fb) { feedback_ = (float)clampd(fb, 0.0, 0.9); }
    void process(float* left, float* right, int numSamples);
private:
    std::vector<float> bufL_, bufR_;
    int writeIdx_ = 0, delaySamples_ = 0;
    float feedback_ = 0.32f;
    double sampleRate_ = 48000;
    Biquad::OnePole lpL_, lpR_;
};

// ------------------------------------------------------------- compressor
// App master bus: DynamicsCompressor threshold -14 dB, ratio 5, attack
// 3 ms, release 180 ms. Simple feedforward peak compressor.
struct MasterCompressor {
    void prepare(double sampleRate);
    void reset();
    void process(float* left, float* right, int numSamples);
private:
    double sampleRate_ = 48000, env_ = 0;
};

// -------------------------------------------------------------- WAV writer
// 16-bit PCM WAV, interleaved stereo, 44.1 kHz default.
std::vector<uint8_t> encodeWavStereo(const float* left, const float* right,
                                     int numSamples, int sampleRate = 44100);
std::vector<uint8_t> encodeWavMono(const float* data, int numSamples,
                                   int sampleRate = 44100);

} // namespace silktone
