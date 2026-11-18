// DspCore.cpp — see DspCore.h. Faithful port of the voice synthesis in
// ~/workspace/your_files/drum-machine/drum-machine.html (playVoice).
#include "DspCore.h"

#include <cstring>
#include <random>

namespace silktone {

// ------------------------------------------------------------------ biquad
void Biquad::set(Type type, double freqHz, double q, double sampleRate) {
    double w = 2.0 * M_PI * clampd(freqHz, 1.0, sampleRate * 0.49) / sampleRate;
    double cw = std::cos(w), sw = std::sin(w);
    double alpha = sw / (2.0 * (q > 0 ? q : 0.8));
    double a0;
    switch (type) {
        case LowPass:
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case HighPass:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case BandPass:
        default:
            b0 = alpha; b1 = 0; b2 = -alpha;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
    }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
}

void Biquad::OnePole::set(double freqHz, double sampleRate) {
    a = 1.0 - std::exp(-2.0 * M_PI * freqHz / sampleRate);
}

void NoiseBuffer::init(double sampleRate) {
    data.assign((size_t)(sampleRate * 1.5), 0.0f);
    uint32_t s = 22222;
    for (auto& v : data) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        v = (float)((int32_t)s) / 2147483648.0f;
    }
    seed = s;
}

float NoiseBuffer::next() {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (float)((int32_t)seed) / 2147483648.0f;
}

// ------------------------------------------------------------ hit renderer
namespace {

enum class OscType { Sine, Triangle, Square };

inline double oscSample(OscType type, double phase) {
    double s = std::sin(phase);
    switch (type) {
        case OscType::Sine: return s;
        case OscType::Triangle: return (2.0 / M_PI) * std::asin(clampd(s, -1.0, 1.0));
        case OscType::Square: return s >= 0 ? 1.0 : -1.0;
    }
    return s;
}

struct Renderer {
    double sr = 48000;
    double ratio = 1.0;   // tune ratio, scales every frequency
    double vel = 1.0;
    NoiseBuffer& noiseBuf;
    std::vector<float>& buf;
    double maxEnd = 0;
    std::mt19937 rng{ 1337 };  // deterministic; the app uses Math.random()

    Renderer(double sampleRate, double tuneRatio, double v, NoiseBuffer& n, std::vector<float>& b)
        : sr(sampleRate), ratio(tuneRatio), vel(v), noiseBuf(n), buf(b) {}

    void ensure(double endSec) {
        size_t need = (size_t)std::ceil(endSec * sr) + 64;
        if (buf.size() < need) buf.resize(need, 0.0f);
        if (endSec > maxEnd) maxEnd = endSec;
    }

    double rand01() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); }

    // tone(type, f0, f1, peak, dec) — pitch glides over min(0.09, dec*0.5),
    // gain env attack 0.001. t0 is the hit-relative start (usually 0).
    void tone(OscType type, double f0, double f1, double peak, double dec, double t0 = 0) {
        double glideT = std::min(0.09, dec * 0.5);
        double f0r = std::max(f0 * ratio, 1.0), f1r = std::max(f1 * ratio, 1.0);
        double endSec = t0 + 0.001 + dec + 0.05;
        ensure(endSec);
        int start = (int)std::lround(t0 * sr);
        int end = (int)std::ceil(endSec * sr);
        double phase = 0;
        double logRatio = std::log(f1r / f0r);
        for (int i = start; i < end; ++i) {
            double t = (double)i / sr - t0;
            double ft = t < glideT ? f0r * std::exp(logRatio * (t / glideT)) : f1r;
            phase += 2.0 * M_PI * ft / sr;
            double g = envGain(t, 0.001, peak, dec);
            if (g == 0.0 && t > 0.001 + dec) break;
            buf[i] += (float)(oscSample(type, phase) * g);
        }
    }

    // noise(dur, type, freq, q, peak, dec, attack) — filtered white noise.
    void noise(double /*dur*/, Biquad::Type type, double freq, double q,
              double peak, double dec, double attack = 0.001, double t0 = 0) {
        Biquad f;
        f.set(type, freq * ratio, q > 0 ? q : 0.8, sr);
        double endSec = t0 + attack + dec + 0.05;
        ensure(endSec);
        int start = (int)std::lround(t0 * sr);
        int end = (int)std::ceil(endSec * sr);
        for (int i = start; i < end; ++i) {
            double t = (double)i / sr - t0;
            double g = envGain(t, attack, peak, dec);
            if (g == 0.0 && t > attack + dec) break;
            buf[i] += (float)(f.process((double)noiseBuf.next()) * g);
        }
    }

    // rings(n, spread, f0, peak) — scattered high metallic bursts.
    void rings(int n, double spread, double f0, double peak, double t0 = 0) {
        for (int k = 0; k < n; ++k) {
            double rt = t0 + 0.015 + k * (spread / std::max(1, n - 1)) * (0.85 + rand01() * 0.3);
            double pk = peak * (1.0 - (double)k / (n + 1)) * (0.7 + rand01() * 0.5);
            double dec = 0.05 + rand01() * 0.03;
            Biquad f;
            f.set(Biquad::HighPass, f0 * ratio, 0.8, sr);
            double endSec = rt + 0.004 + dec + 0.05;
            ensure(endSec);
            int start = (int)std::lround(rt * sr);
            int end = (int)std::ceil(endSec * sr);
            for (int i = start; i < end; ++i) {
                double t = (double)i / sr - rt;
                double g = envGain(t, 0.004, std::max(pk, 0.0002), dec);
                if (g == 0.0 && t > 0.004 + dec) break;
                buf[i] += (float)(f.process((double)noiseBuf.next()) * g);
            }
        }
    }

    // Gated-reverb wash for the 'gated' snare: lowpass noise, hold then chop.
    void gatedWash(double bright, double t0 = 0) {
        Biquad f;
        f.set(Biquad::LowPass, 3200 * bright * ratio, 0.8, sr);
        double endSec = t0 + 0.32;
        ensure(endSec);
        int start = (int)std::lround(t0 * sr);
        int end = (int)std::ceil(endSec * sr);
        for (int i = start; i < end; ++i) {
            double t = (double)i / sr - t0;
            double g;
            if (t < 0.012) g = 0.0001 + (0.34 * vel - 0.0001) * (t / 0.012);
            else if (t < 0.2) g = 0.34 * vel;
            else if (t < 0.235) g = 0.34 * vel + (0.0001 - 0.34 * vel) * ((t - 0.2) / 0.035);
            else g = 0.0;
            if (g == 0.0) break;
            buf[i] += (float)(f.process((double)noiseBuf.next()) * g);
        }
    }

    // Simmons tom: swept sine + triangle harmonic, long glide times.
    void simTom(double f0, double f1, double dec, double peak, double t0 = 0) {
        auto osc = [&](OscType type, double a, double b, double glideT, double pk, double dc) {
            double ar = std::max(a * ratio, 20.0), br = std::max(b * ratio, 20.0);
            double endSec = t0 + 0.002 + dc + 0.05;
            ensure(endSec);
            int start = (int)std::lround(t0 * sr);
            int end = (int)std::ceil(endSec * sr);
            double phase = 0, lr = std::log(br / ar);
            for (int i = start; i < end; ++i) {
                double t = (double)i / sr - t0;
                double ft = t < glideT ? ar * std::exp(lr * (t / glideT)) : br;
                phase += 2.0 * M_PI * ft / sr;
                double g = envGain(t, 0.002, pk, dc);
                if (g == 0.0 && t > 0.002 + dc) break;
                buf[i] += (float)(oscSample(type, phase) * g);
            }
        };
        osc(OscType::Sine, f0, f1, dec * 0.82, peak, dec);
        osc(OscType::Triangle, f0 * 2, f1 * 2, dec * 0.6, peak * 0.16, dec * 0.55);
        noise(0.025, Biquad::BandPass, 1500, 1.2, 0.2 * vel, 0.018, 0.001, t0);
    }

    // Amapiano log tone: sine + soft harmonic + lowpass noise tick.
    void logTone(double f0, double peak, double dec, double t0 = 0) {
        tone(OscType::Sine, f0, f0 * 0.52, peak, dec, t0);
        tone(OscType::Triangle, f0 * 2, f0 * 1.04, peak * 0.14, dec * 0.5, t0);
        noise(0.02, Biquad::LowPass, 900, 1.0, 0.1 * vel, 0.014, 0.001, t0);
    }

    // Default-kit clap: three bandpass noise bursts at 12 ms spacing.
    void clapBursts(double bright, double t0 = 0) {
        for (int k = 0; k < 3; ++k) {
            double tt = t0 + k * 0.012;
            double pk = (k == 2 ? 0.85 : 0.5) * vel;
            double dc = k == 2 ? 0.22 : 0.03;
            Biquad f;
            f.set(Biquad::BandPass, 1300 * bright * ratio, 1.6, sr);
            double endSec = tt + 0.001 + dc + 0.05;
            ensure(endSec);
            int start = (int)std::lround(tt * sr);
            int end = (int)std::ceil(endSec * sr);
            for (int i = start; i < end; ++i) {
                double t = (double)i / sr - tt;
                double g = envGain(t, 0.001, pk, dc);
                if (g == 0.0 && t > 0.001 + dc) break;
                buf[i] += (float)(f.process((double)noiseBuf.next()) * g);
            }
        }
    }

    // Vinyl crackle: 14 random bandpass pops over 0.34s.
    void cracklePops(double t0 = 0) {
        for (int p = 0; p < 14; ++p) {
            double pt = t0 + rand01() * 0.34;
            double pk = (0.12 + rand01() * 0.5) * vel;
            double dec = 0.012 + rand01() * 0.02;
            Biquad f;
            f.set(Biquad::BandPass, (1400 + rand01() * 2600) * ratio, 9.0, sr);
            double endSec = pt + 0.002 + dec + 0.05;
            ensure(endSec);
            int start = (int)std::lround(pt * sr);
            int end = (int)std::ceil(endSec * sr);
            for (int i = start; i < end; ++i) {
                double t = (double)i / sr - pt;
                double g = envGain(t, 0.002, pk, dec);
                if (g == 0.0 && t > 0.002 + dec) break;
                buf[i] += (float)(f.process((double)noiseBuf.next()) * g);
            }
        }
    }
};

} // namespace

void renderVoiceHit(const char* voiceId, const KitParams& K, double velocity,
                    double tuneRatio, double sampleRate, NoiseBuffer& noise,
                    HitBuffer& out) {
    std::string id = voiceId ? voiceId : "kick";
    double v = clampd(velocity, 0.0, 1.5);
    std::vector<float> buf;
    buf.reserve((size_t)(sampleRate * 0.5));
    Renderer r(sampleRate, tuneRatio, v, noise, buf);
    const double br = K.bright;

    // ------------------------------------------------ extra voices (kit-independent)
    if (id == "brush" || id == "gated" || id == "sub" || id == "timbale" ||
        id == "djembe" || id == "agogo" || id == "crackle" || id == "tombak" ||
        id == "tombakbak" || id == "daf" || id == "dayereh" || id == "dohol" ||
        id == "darbuka" || id == "dartek" || id == "riq") {
        if (id == "brush") {
            r.noise(0.34, Biquad::BandPass, 2100 * br, 0.6, 0.5 * v, 0.26, 0.035);
            r.noise(0.2, Biquad::HighPass, 6000 * br, 1.0, 0.18 * v, 0.14, 0.02);
            r.tone(OscType::Triangle, K.snareT * 0.95, K.snareT * 0.75, 0.3 * v, 0.07);
        } else if (id == "gated") {
            r.noise(0.3, Biquad::BandPass, 1900 * br, 0.7, 0.72 * v, 0.16);
            r.noise(0.06, Biquad::HighPass, 5400 * br, 1.0, 0.3 * v, 0.04);
            r.tone(OscType::Triangle, K.snareT * 1.05, K.snareT * 0.8, 0.55 * v, 0.1);
            r.gatedWash(br);
        } else if (id == "sub") {
            r.tone(OscType::Sine, 118, 34, 1.0 * v, 0.95);
            r.tone(OscType::Sine, 59, 30, 0.35 * v, 0.7);
            r.noise(0.02, Biquad::LowPass, 900, 1.0, 0.2 * v, 0.015);
        } else if (id == "timbale") {
            r.tone(OscType::Square, 392, 330, 0.22 * v, 0.16);
            r.tone(OscType::Sine, 587, 540, 0.18 * v, 0.12);
            r.noise(0.05, Biquad::BandPass, 2600, 3.0, 0.4 * v, 0.035);
        } else if (id == "djembe") {
            r.tone(OscType::Sine, 186, 92, 0.85 * v, 0.3);
            r.tone(OscType::Triangle, 372, 220, 0.2 * v, 0.09);
            r.noise(0.04, Biquad::BandPass, 950, 1.2, 0.34 * v, 0.03);
        } else if (id == "agogo") {
            r.tone(OscType::Square, 830, 824, 0.26 * v, 0.22);
            r.tone(OscType::Square, 1108, 1100, 0.2 * v, 0.17);
            r.tone(OscType::Sine, 1660, 1650, 0.1 * v, 0.1);
        } else if (id == "crackle") {
            r.noise(0.4, Biquad::LowPass, 2600, 0.6, 0.10 * v, 0.34, 0.005);
            r.cracklePops();
        } else if (id == "tombak") {
            r.tone(OscType::Sine, 168, 66, 0.95 * v, 0.4);
            r.tone(OscType::Triangle, 336, 138, 0.16 * v, 0.11);
            r.tone(OscType::Sine, 252, 238, 0.07 * v, 0.22);
            r.noise(0.035, Biquad::BandPass, 820, 1.1, 0.3 * v, 0.026);
        } else if (id == "tombakbak") {
            r.tone(OscType::Sine, 348, 186, 0.6 * v, 0.13);
            r.tone(OscType::Triangle, 640, 420, 0.14 * v, 0.06);
            r.noise(0.05, Biquad::BandPass, 1950, 1.4, 0.55 * v, 0.038);
            r.noise(0.02, Biquad::HighPass, 5200, 1.0, 0.2 * v, 0.014);
        } else if (id == "daf") {
            r.tone(OscType::Sine, 126, 56, 0.95 * v, 0.5);
            r.tone(OscType::Triangle, 252, 112, 0.14 * v, 0.14);
            r.noise(0.06, Biquad::BandPass, 520, 1.0, 0.22 * v, 0.045);
            r.rings(8, 0.5, 7600, 0.16 * v);
        } else if (id == "dayereh") {
            r.tone(OscType::Sine, 188, 86, 0.85 * v, 0.34);
            r.tone(OscType::Triangle, 376, 172, 0.12 * v, 0.09);
            r.noise(0.05, Biquad::BandPass, 1150, 1.0, 0.26 * v, 0.036);
            r.rings(9, 0.38, 9000, 0.15 * v);
        } else if (id == "dohol") {
            r.tone(OscType::Sine, 94, 37, 1.0 * v, 0.68);
            r.tone(OscType::Sine, 141, 58, 0.4 * v, 0.4);
            r.tone(OscType::Triangle, 188, 74, 0.12 * v, 0.12);
            r.noise(0.04, Biquad::LowPass, 760, 1.0, 0.3 * v, 0.03);
        } else if (id == "darbuka") {
            r.tone(OscType::Sine, 182, 74, 0.98 * v, 0.36);
            r.tone(OscType::Triangle, 364, 150, 0.15 * v, 0.09);
            r.tone(OscType::Sine, 428, 414, 0.06 * v, 0.2);
            r.noise(0.03, Biquad::BandPass, 940, 1.1, 0.3 * v, 0.024);
        } else if (id == "dartek") {
            r.tone(OscType::Square, 884, 828, 0.26 * v, 0.07);
            r.tone(OscType::Sine, 1330, 1290, 0.1 * v, 0.09);
            r.noise(0.04, Biquad::BandPass, 3300, 5.0, 0.55 * v, 0.03);
        } else if (id == "riq") {
            r.tone(OscType::Sine, 214, 102, 0.7 * v, 0.24);
            r.tone(OscType::Triangle, 428, 204, 0.1 * v, 0.07);
            r.noise(0.05, Biquad::BandPass, 1500, 1.2, 0.3 * v, 0.04);
            r.rings(11, 0.55, 8400, 0.2 * v);
        }
        out.samples = std::move(buf);
        out.numSamples = (int)std::ceil(r.maxEnd * sampleRate) + 1;
        return;
    }

    // ------------------------------------------------ kit branch dispatch
    // Mirrors playVoice(): acoustic -> real/dusty family -> linndrum -> dmx
    // -> sp1200 -> simmons -> phonk -> drill -> jersey -> logdrum -> linn
    // -> cr78 -> generic machine branch.
    // tone(type, f0, f1, peak, dec); noise(dur, type, freq, q, peak, dec, attack).
    const std::string branch = K.branch ? K.branch : "generic";
    auto T = OscType::Triangle, S = OscType::Sine, Q = OscType::Square;

    if (branch == "acoustic") {
        if (id == "kick") { r.tone(S,112,44,0.98*v,0.42); r.tone(T,230,95,0.22*v,0.07); r.noise(0.025,Biquad::LowPass,1100,1.0,0.28*v,0.02); }
        else if (id == "snare") { r.noise(0.22,Biquad::BandPass,1900,0.7,0.62*v,0.11); r.noise(0.06,Biquad::HighPass,5600,1.0,0.32*v,0.045); r.tone(T,186,148,0.55*v,0.085); }
        else if (id == "clap") { r.noise(0.18,Biquad::BandPass,1250,1.4,0.5*v,0.14); r.noise(0.05,Biquad::HighPass,4200,1.0,0.22*v,0.04); }
        else if (id == "chat") { r.noise(0.07,Biquad::HighPass,7900,1.0,0.48*v,0.052); r.tone(Q,9200,8600,0.028*v,0.05); }
        else if (id == "ohat") { r.noise(0.55,Biquad::HighPass,7600,1.0,0.5*v,0.48); r.tone(Q,9100,8500,0.06*v,0.04); }
        else if (id == "ltom") { r.tone(S,168,86,0.4*v,0.85); r.tone(T,336,172,0.12*v,0.16); r.noise(0.02,Biquad::LowPass,900,1.0,0.15*v,0.015); }
        else if (id == "htom") { r.tone(S,242,122,0.34*v,0.85); r.tone(T,484,244,0.1*v,0.15); r.noise(0.02,Biquad::LowPass,1100,1.0,0.13*v,0.015); }
        else if (id == "rim") { r.tone(Q,1680,1500,0.045*v,0.2); r.noise(0.04,Biquad::BandPass,2900,5.0,0.5*v,0.03); }
        else if (id == "crash") { r.noise(1.7,Biquad::HighPass,5900,0.7,0.5*v,1.55); r.noise(1.1,Biquad::BandPass,8800,2.0,0.22*v,0.9); }
        else if (id == "shaker") { r.noise(0.13,Biquad::BandPass,6300,1.1,0.4*v,0.08,0.012); }
        else if (id == "conga") { r.tone(S,196,118,0.26*v,0.8); r.tone(T,392,236,0.07*v,0.14); r.noise(0.03,Biquad::BandPass,900,1.0,0.3*v,0.025); }
        else if (id == "bongo") { r.tone(S,296,178,0.18*v,0.75); r.tone(T,592,356,0.05*v,0.12); r.noise(0.02,Biquad::BandPass,1300,1.0,0.26*v,0.018); }
        else if (id == "tamb") { r.noise(0.16,Biquad::HighPass,8200,1.0,0.34*v,0.09); r.noise(0.1,Biquad::BandPass,10400,3.0,0.26*v,0.06,0.004); }
        else if (id == "cow") { r.tone(Q,587,580,0.32*v,0.3); r.tone(Q,845,838,0.26*v,0.24); }
        else if (id == "clave") { r.tone(S,2540,2380,0.055*v,0.5); r.noise(0.02,Biquad::BandPass,3600,4.0,0.3*v,0.015); }
    } else if (K.real > 0) {
        // Real & dusty family — Black Beauty, Muffled 70s, Funk Break,
        // Soul Dust, Dilla Dust, Shaolin Dust.
        double mf = K.muffle, ring = K.ring, snap = K.snap > 0 ? K.snap : 0.8;
        int dust = K.dust > 0 ? 1 : 0, grit = K.grit > 0 ? 1 : 0, lofi = K.lofi > 0 ? 1 : 0;
        double hatHz = 8200 * br * (1 - mf * 0.28), decScale = 1 - mf * 0.38;
        if (dust && (id == "kick" || id == "snare")) r.noise(0.09, Biquad::LowPass, 900, 1.0, 0.14 * v, 0.07);
        if (id == "kick") { r.tone(S,K.kickF,K.kickF*0.38,0.98*v,K.kickD*decScale); r.tone(T,K.kickF*2,K.kickF*0.85,(0.2-mf*0.08)*v,0.06); r.noise(0.025,Biquad::LowPass,1000-mf*350,1.0,(0.26+grit*0.08)*v,0.018); if(lofi) r.tone(S,K.kickF*0.5,38,0.22*v,0.18); }
        else if (id == "snare") {
            r.noise(0.22,Biquad::BandPass,1750*br,0.7,(0.58+snap*0.14)*v,0.105*decScale+(grit?0.02:0));
            r.noise(0.06,Biquad::HighPass,5200*br,1.0,0.3*v,0.04*decScale);
            r.tone(T,K.snareT,K.snareT*0.78,(0.5+snap*0.12)*v,0.082*decScale);
            if (ring > 0.1) { r.tone(S,492,478,0.16*ring*v,0.19); r.tone(S,712,694,0.09*ring*v,0.13); }
            if (dust) r.noise(0.14,Biquad::BandPass,980,1.0,0.18*v,0.09);
            if (lofi) r.noise(0.05,Biquad::BandPass,2600,2.0,0.16*v,0.035);
        }
        else if (id == "clap") { r.noise(0.18,Biquad::BandPass,1180*br,1.4,0.48*v,0.13*decScale); r.noise(0.05,Biquad::HighPass,3800,1.0,0.2*v,0.035); if(dust) r.noise(0.08,Biquad::LowPass,1200,1.0,0.12*v,0.06); }
        else if (id == "chat") { r.noise(0.07,Biquad::HighPass,hatHz,1.0,0.46*v,0.05*K.hatD*decScale); r.tone(Q,hatHz+900,hatHz+300,0.04*v,0.024); }
        else if (id == "ohat") { r.noise(0.5,Biquad::HighPass,hatHz-300,1.0,0.48*v,0.44*K.hatD*decScale); r.tone(Q,hatHz+700,hatHz+100,0.035*v,0.05); }
        else if (id == "ltom") { r.tone(S,158,78,0.84*v,0.36*decScale); r.tone(T,316,156,0.13*v,0.1*decScale); r.noise(0.02,Biquad::LowPass,820,1.0,0.13*v,0.014); }
        else if (id == "htom") { r.tone(S,228,112,0.84*v,0.3*decScale); r.tone(T,456,224,0.12*v,0.085*decScale); r.noise(0.02,Biquad::LowPass,980,1.0,0.11*v,0.014); }
        else if (id == "rim") { r.tone(Q,1640,1480,0.19*v,0.042*decScale); r.noise(0.04,Biquad::BandPass,2750,5.0,(0.44+grit*0.14)*v,0.028); if(ring>0.5) r.tone(S,880,860,0.08*v,0.06); }
        else if (id == "crash") { r.noise(1.5,Biquad::HighPass,5400*br,0.7,0.48*v,1.3*decScale); r.noise(1.0,Biquad::BandPass,8200*br,2.0,0.2*v,0.8*decScale); }
        else if (id == "shaker") { r.noise(0.13,Biquad::BandPass,5900*br,1.1,0.4*v,0.075,0.012); if(dust) r.noise(0.09,Biquad::BandPass,3400,1.0,0.1*v,0.05,0.01); }
        else if (id == "conga") { r.tone(S,192*br,114,0.8*v,0.25); r.tone(T,384,228,0.13*v,0.065); r.noise(0.03,Biquad::BandPass,880,1.0,0.28*v,0.022); }
        else if (id == "bongo") { r.tone(S,292*br,174,0.75*v,0.17); r.tone(T,584,348,0.11*v,0.048); r.noise(0.02,Biquad::BandPass,1260,1.0,0.24*v,0.016); }
        else if (id == "tamb") { r.noise(0.15,Biquad::HighPass,7800*br,1.0,0.32*v,0.085*decScale); r.noise(0.09,Biquad::BandPass,9900*br,3.0,0.22*v,0.055,0.004); }
        else if (id == "cow") { r.tone(Q,584,578,0.29*v,0.3*decScale); r.tone(Q,842,834,0.23*v,0.24*decScale); }
        else if (id == "clave") { r.tone(S,2480,2330,0.48*v,0.052); r.noise(0.02,Biquad::BandPass,3450,4.0,0.28*v,0.014); }
    } else if (branch == "linndrum") {
        if (id == "kick") { r.tone(S,124,44,1.0*v,0.30); r.tone(T,248,96,0.2*v,0.05); r.noise(0.02,Biquad::BandPass,2100,1.0,0.32*v,0.016); }
        else if (id == "snare") { r.noise(0.18,Biquad::BandPass,2100,0.8,0.7*v,0.12); r.tone(T,220,162,0.56*v,0.075); r.noise(0.04,Biquad::HighPass,6400,1.0,0.24*v,0.03); r.tone(S,494,482,0.07*v,0.1); }
        else if (id == "clap") { r.noise(0.26,Biquad::BandPass,1420,1.5,0.62*v,0.21); r.noise(0.05,Biquad::HighPass,4300,1.0,0.2*v,0.04); }
        else if (id == "chat") { r.noise(0.055,Biquad::HighPass,8800,1.0,0.52*v,0.04); r.tone(Q,10200,9700,0.06*v,0.018); }
        else if (id == "ohat") { r.noise(0.3,Biquad::HighPass,8600,1.0,0.5*v,0.26); r.tone(Q,10000,9500,0.04*v,0.04); }
        else if (id == "ltom") { r.tone(S,178,88,0.88*v,0.3); r.tone(T,356,176,0.13*v,0.09); r.noise(0.018,Biquad::LowPass,1000,1.0,0.12*v,0.014); }
        else if (id == "htom") { r.tone(S,252,124,0.88*v,0.26); r.tone(T,504,248,0.12*v,0.07); r.noise(0.018,Biquad::LowPass,1200,1.0,0.11*v,0.014); }
        else if (id == "rim") { r.tone(Q,1760,1680,0.24*v,0.05); r.noise(0.03,Biquad::BandPass,3200,5.0,0.44*v,0.024); }
        else if (id == "crash") { r.noise(1.15,Biquad::HighPass,7000,0.7,0.52*v,1.0); r.noise(0.7,Biquad::BandPass,9200,2.0,0.18*v,0.55); }
        else if (id == "shaker") { r.noise(0.09,Biquad::BandPass,7000,1.2,0.44*v,0.05,0.008); r.noise(0.07,Biquad::BandPass,7600,1.2,0.3*v,0.04,0.045); }
        else if (id == "conga") { r.tone(S,218,128,0.82*v,0.23); r.tone(T,436,256,0.11*v,0.06); }
        else if (id == "bongo") { r.tone(S,322,192,0.78*v,0.16); r.tone(T,644,384,0.1*v,0.045); }
        else if (id == "tamb") { r.noise(0.13,Biquad::HighPass,9000,1.0,0.38*v,0.07); r.noise(0.08,Biquad::BandPass,10800,3.0,0.22*v,0.045,0.004); }
        else if (id == "cow") { r.tone(Q,568,562,0.3*v,0.27); r.tone(Q,824,816,0.24*v,0.21); }
        else if (id == "clave") { r.tone(Q,2580,2460,0.34*v,0.05); }
    } else if (branch == "dmx") {
        if (id == "kick") { r.tone(S,132,46,0.98*v,0.27); r.noise(0.018,Biquad::BandPass,2400,1.0,0.3*v,0.014); }
        else if (id == "snare") { r.noise(0.15,Biquad::BandPass,2250,0.8,0.68*v,0.10); r.tone(T,208,156,0.52*v,0.065); r.noise(0.035,Biquad::HighPass,6800,1.0,0.22*v,0.026); }
        else if (id == "clap") { r.noise(0.2,Biquad::BandPass,1500,1.6,0.56*v,0.16); }
        else if (id == "chat") { r.noise(0.05,Biquad::HighPass,9000,1.0,0.52*v,0.036); r.tone(Q,10400,9900,0.06*v,0.016); }
        else if (id == "ohat") { r.noise(0.26,Biquad::HighPass,8800,1.0,0.5*v,0.22); }
        else if (id == "ltom") { r.tone(S,164,82,0.85*v,0.25); r.tone(T,328,164,0.11*v,0.07); }
        else if (id == "htom") { r.tone(S,234,116,0.85*v,0.21); r.tone(T,468,232,0.1*v,0.055); }
        else if (id == "rim") { r.tone(Q,1820,1720,0.23*v,0.045); r.noise(0.028,Biquad::BandPass,3400,5.0,0.42*v,0.02); }
        else if (id == "crash") { r.noise(0.95,Biquad::HighPass,7200,0.7,0.5*v,0.82); }
        else if (id == "shaker") { r.noise(0.1,Biquad::BandPass,6800,1.1,0.46*v,0.06,0.009); }
        else if (id == "conga") { r.tone(S,226,134,0.8*v,0.2); }
        else if (id == "bongo") { r.tone(S,332,198,0.76*v,0.14); }
        else if (id == "tamb") { r.noise(0.14,Biquad::HighPass,9200,1.0,0.4*v,0.08); r.noise(0.08,Biquad::BandPass,11000,3.0,0.24*v,0.05,0.004); }
        else if (id == "cow") { r.tone(Q,574,568,0.3*v,0.26); r.tone(Q,832,824,0.24*v,0.2); }
        else if (id == "clave") { r.tone(Q,2620,2500,0.33*v,0.048); }
    } else if (branch == "sp1200") {
        if (id == "kick") { r.tone(S,108,40,0.98*v,0.42); r.tone(T,216,84,0.18*v,0.06); r.noise(0.03,Biquad::LowPass,950,1.0,0.3*v,0.024); r.noise(0.05,Biquad::LowPass,700,1.0,0.14*v,0.04); }
        else if (id == "snare") { r.noise(0.2,Biquad::BandPass,1680,0.7,0.66*v,0.11); r.noise(0.05,Biquad::HighPass,4600,1.0,0.24*v,0.036); r.tone(T,182,140,0.54*v,0.08); r.noise(0.1,Biquad::BandPass,900,1.0,0.16*v,0.07); }
        else if (id == "clap") { r.noise(0.18,Biquad::BandPass,1150,1.4,0.5*v,0.13); r.noise(0.06,Biquad::LowPass,1300,1.0,0.14*v,0.05); }
        else if (id == "chat") { r.noise(0.065,Biquad::HighPass,7200,1.0,0.48*v,0.05); r.tone(Q,8400,7900,0.07*v,0.026); }
        else if (id == "ohat") { r.noise(0.42,Biquad::HighPass,7000,1.0,0.48*v,0.36); r.tone(Q,8300,7800,0.05*v,0.05); }
        else if (id == "ltom") { r.tone(S,152,74,0.85*v,0.34); r.tone(T,304,148,0.12*v,0.09); r.noise(0.02,Biquad::LowPass,800,1.0,0.13*v,0.015); }
        else if (id == "htom") { r.tone(S,218,106,0.85*v,0.29); r.tone(T,436,212,0.11*v,0.075); r.noise(0.02,Biquad::LowPass,950,1.0,0.12*v,0.015); }
        else if (id == "rim") { r.tone(Q,1560,1420,0.2*v,0.042); r.noise(0.04,Biquad::BandPass,2500,5.0,0.46*v,0.028); }
        else if (id == "crash") { r.noise(1.25,Biquad::HighPass,5200,0.7,0.48*v,1.05); r.noise(0.8,Biquad::BandPass,7600,2.0,0.16*v,0.6); }
        else if (id == "shaker") { r.noise(0.12,Biquad::BandPass,5600,1.1,0.4*v,0.07,0.011); r.noise(0.08,Biquad::BandPass,3200,1.0,0.1*v,0.05,0.01); }
        else if (id == "conga") { r.tone(S,188,110,0.8*v,0.25); r.noise(0.028,Biquad::BandPass,820,1.0,0.26*v,0.02); }
        else if (id == "bongo") { r.tone(S,282,168,0.76*v,0.17); r.noise(0.02,Biquad::BandPass,1180,1.0,0.22*v,0.015); }
        else if (id == "tamb") { r.noise(0.14,Biquad::HighPass,7400,1.0,0.33*v,0.08); r.noise(0.08,Biquad::BandPass,9200,3.0,0.2*v,0.05,0.004); }
        else if (id == "cow") { r.tone(Q,578,572,0.29*v,0.29); r.tone(Q,838,830,0.23*v,0.23); }
        else if (id == "clave") { r.tone(S,2380,2240,0.46*v,0.052); r.noise(0.018,Biquad::BandPass,3200,4.0,0.24*v,0.013); }
    } else if (branch == "simmons") {
        if (id == "kick") { r.simTom(150,42,0.34,0.95*v); }
        else if (id == "snare") { r.noise(0.16,Biquad::BandPass,2000,0.8,0.6*v,0.1); r.simTom(220,120,0.14,0.4*v); r.noise(0.04,Biquad::HighPass,6000,1.0,0.22*v,0.03); }
        else if (id == "clap") { r.noise(0.18,Biquad::BandPass,1350,1.5,0.5*v,0.14); r.simTom(300,190,0.09,0.16*v); }
        else if (id == "chat") { r.noise(0.06,Biquad::HighPass,8600,1.0,0.48*v,0.045); r.tone(Q,9800,9300,0.06*v,0.022); }
        else if (id == "ohat") { r.noise(0.4,Biquad::HighPass,8400,1.0,0.48*v,0.34); }
        else if (id == "ltom") { r.simTom(196,62,0.5,0.95*v); }
        else if (id == "htom") { r.simTom(294,92,0.44,0.95*v); }
        else if (id == "rim") { r.simTom(720,340,0.09,0.32*v); r.noise(0.03,Biquad::BandPass,3100,5.0,0.4*v,0.022); }
        else if (id == "crash") { r.noise(1.2,Biquad::HighPass,6800,0.7,0.5*v,1.02); }
        else if (id == "shaker") { r.noise(0.11,Biquad::BandPass,6400,1.1,0.4*v,0.065,0.01); }
        else if (id == "conga") { r.simTom(392,128,0.4,0.9*v); }
        else if (id == "bongo") { r.simTom(523,176,0.36,0.88*v); }
        else if (id == "tamb") { r.noise(0.14,Biquad::HighPass,8600,1.0,0.35*v,0.08); }
        else if (id == "cow") { r.tone(Q,566,560,0.3*v,0.3); r.tone(Q,822,814,0.24*v,0.24); }
        else if (id == "clave") { r.simTom(880,520,0.07,0.3*v); }
    } else if (branch == "phonk") {
        if (id == "kick") { r.tone(S,118,38,1.0*v,0.4); r.tone(Q,118,52,0.22*v,0.09); r.noise(0.025,Biquad::LowPass,1100,1.0,0.3*v,0.02); }
        else if (id == "snare") { r.noise(0.17,Biquad::BandPass,1950,0.75,0.7*v,0.105); r.tone(T,188,144,0.54*v,0.075); r.noise(0.04,Biquad::HighPass,5400,1.0,0.26*v,0.03); }
        else if (id == "clap") { r.noise(0.18,Biquad::BandPass,1250,1.5,0.54*v,0.14); }
        else if (id == "chat") { r.noise(0.06,Biquad::HighPass,7800,1.0,0.5*v,0.045); r.tone(Q,9000,8500,0.05*v,0.022); }
        else if (id == "ohat") { r.noise(0.34,Biquad::HighPass,7600,1.0,0.48*v,0.29); }
        else if (id == "ltom") { r.tone(S,146,68,0.86*v,0.32); r.noise(0.02,Biquad::LowPass,820,1.0,0.13*v,0.015); }
        else if (id == "htom") { r.tone(S,208,98,0.86*v,0.27); r.noise(0.02,Biquad::LowPass,980,1.0,0.12*v,0.015); }
        else if (id == "rim") { r.tone(Q,1620,1480,0.21*v,0.042); r.noise(0.038,Biquad::BandPass,2800,5.0,0.46*v,0.027); }
        else if (id == "crash") { r.noise(1.1,Biquad::HighPass,5800,0.7,0.48*v,0.94); }
        else if (id == "shaker") { r.noise(0.11,Biquad::BandPass,6000,1.1,0.4*v,0.062,0.01); }
        else if (id == "conga") { r.tone(S,196,116,0.8*v,0.24); }
        else if (id == "bongo") { r.tone(S,294,176,0.76*v,0.16); }
        else if (id == "tamb") { r.noise(0.13,Biquad::HighPass,8000,1.0,0.34*v,0.075); }
        else if (id == "cow") { r.tone(Q,587,580,0.36*v,0.48); r.tone(Q,845,838,0.29*v,0.4); r.tone(S,1174,1168,0.12*v,0.22); }
        else if (id == "clave") { r.tone(S,2460,2320,0.48*v,0.052); }
    } else if (branch == "drill") {
        if (id == "kick") { r.tone(S,122,40,0.99*v,0.36); r.noise(0.02,Biquad::BandPass,2300,1.0,0.28*v,0.015); }
        else if (id == "snare") { r.noise(0.16,Biquad::BandPass,2150,0.8,0.68*v,0.1); r.tone(T,202,154,0.52*v,0.068); r.noise(0.035,Biquad::HighPass,6600,1.0,0.24*v,0.027); }
        else if (id == "clap") { r.noise(0.17,Biquad::BandPass,1380,1.5,0.52*v,0.13); }
        else if (id == "chat") { r.noise(0.05,Biquad::HighPass,9200,1.0,0.52*v,0.036); r.tone(Q,10600,10100,0.055*v,0.015); }
        else if (id == "ohat") { r.noise(0.28,Biquad::HighPass,9000,1.0,0.5*v,0.24); }
        else if (id == "ltom") { r.tone(S,150,70,0.84*v,0.3); }
        else if (id == "htom") { r.tone(S,216,102,0.84*v,0.25); }
        else if (id == "rim") { r.tone(Q,1740,1620,0.22*v,0.044); r.noise(0.03,Biquad::BandPass,3300,5.0,0.42*v,0.022); }
        else if (id == "crash") { r.noise(1.0,Biquad::HighPass,6600,0.7,0.48*v,0.85); }
        else if (id == "shaker") { r.noise(0.1,Biquad::BandPass,6600,1.1,0.4*v,0.058,0.009); }
        else if (id == "conga") { r.tone(S,204,120,0.79*v,0.22); }
        else if (id == "bongo") { r.tone(S,304,182,0.75*v,0.15); }
        else if (id == "tamb") { r.noise(0.12,Biquad::HighPass,8800,1.0,0.34*v,0.07); }
        else if (id == "cow") { r.tone(Q,580,574,0.29*v,0.27); r.tone(Q,840,832,0.23*v,0.21); }
        else if (id == "clave") { r.tone(Q,2540,2420,0.32*v,0.05); }
    } else if (branch == "jersey") {
        if (id == "kick") { r.tone(S,150,48,0.98*v,0.3); r.tone(S,196,88,0.3*v,0.14); r.noise(0.018,Biquad::BandPass,2200,1.0,0.26*v,0.013); }
        else if (id == "snare") { r.noise(0.15,Biquad::BandPass,2050,0.8,0.62*v,0.095); r.tone(T,206,158,0.48*v,0.062); }
        else if (id == "clap") { r.noise(0.22,Biquad::BandPass,1450,1.5,0.6*v,0.18); r.noise(0.04,Biquad::HighPass,4600,1.0,0.2*v,0.032); }
        else if (id == "chat") { r.noise(0.05,Biquad::HighPass,9000,1.0,0.5*v,0.037); }
        else if (id == "ohat") { r.noise(0.24,Biquad::HighPass,8800,1.0,0.48*v,0.2); }
        else if (id == "ltom") { r.tone(S,172,80,0.86*v,0.27); r.tone(T,344,160,0.12*v,0.07); }
        else if (id == "htom") { r.tone(S,244,116,0.86*v,0.23); r.tone(T,488,232,0.11*v,0.06); }
        else if (id == "rim") { r.tone(Q,1780,1660,0.23*v,0.046); r.noise(0.03,Biquad::BandPass,3250,5.0,0.42*v,0.022); }
        else if (id == "crash") { r.noise(0.9,Biquad::HighPass,7000,0.7,0.48*v,0.76); }
        else if (id == "shaker") { r.noise(0.1,Biquad::BandPass,6800,1.1,0.42*v,0.058,0.009); }
        else if (id == "conga") { r.tone(S,214,126,0.8*v,0.21); }
        else if (id == "bongo") { r.tone(S,318,190,0.76*v,0.14); }
        else if (id == "tamb") { r.noise(0.13,Biquad::HighPass,9000,1.0,0.37*v,0.075); }
        else if (id == "cow") { r.tone(Q,572,566,0.3*v,0.26); r.tone(Q,830,822,0.24*v,0.2); }
        else if (id == "clave") { r.tone(Q,2600,2480,0.33*v,0.049); }
    } else if (branch == "logdrum") {
        if (id == "kick") { r.tone(S,102,42,0.94*v,0.36); r.noise(0.022,Biquad::LowPass,950,1.0,0.22*v,0.017); }
        else if (id == "snare") { r.noise(0.17,Biquad::BandPass,1780,0.75,0.56*v,0.1); r.tone(T,184,142,0.44*v,0.07); }
        else if (id == "clap") { r.noise(0.17,Biquad::BandPass,1220,1.4,0.46*v,0.125); }
        else if (id == "chat") { r.noise(0.065,Biquad::HighPass,8000,1.0,0.44*v,0.05); }
        else if (id == "ohat") { r.noise(0.42,Biquad::HighPass,7800,1.0,0.44*v,0.36); }
        else if (id == "ltom") { r.logTone(164,0.92*v,0.42); }
        else if (id == "htom") { r.logTone(232,0.9*v,0.36); }
        else if (id == "rim") { r.tone(Q,1600,1460,0.18*v,0.04); r.noise(0.036,Biquad::BandPass,2700,5.0,0.4*v,0.026); }
        else if (id == "crash") { r.noise(1.3,Biquad::HighPass,6000,0.7,0.44*v,1.1); }
        else if (id == "shaker") { r.noise(0.13,Biquad::BandPass,6100,1.1,0.46*v,0.078,0.012); r.noise(0.09,Biquad::BandPass,6800,1.1,0.3*v,0.05,0.05); }
        else if (id == "conga") { r.logTone(208,0.84*v,0.3); }
        else if (id == "bongo") { r.logTone(308,0.82*v,0.24); }
        else if (id == "tamb") { r.noise(0.14,Biquad::HighPass,8200,1.0,0.33*v,0.082); }
        else if (id == "cow") { r.tone(Q,582,576,0.28*v,0.28); r.tone(Q,840,832,0.22*v,0.22); }
        else if (id == "clave") { r.tone(S,2440,2300,0.46*v,0.052); }
    } else if (branch == "linn") {
        if (id == "kick") { r.tone(S,126,46,0.98*v,0.26); r.noise(0.02,Biquad::BandPass,1800,1.0,0.3*v,0.016); }
        else if (id == "snare") { r.noise(0.16,Biquad::BandPass,2050,0.8,0.66*v,0.11); r.tone(T,218,164,0.52*v,0.07); r.noise(0.04,Biquad::HighPass,6200,1.0,0.22*v,0.03); }
        else if (id == "clap") { r.noise(0.22,Biquad::BandPass,1380,1.5,0.58*v,0.18); }
        else if (id == "chat") { r.noise(0.06,Biquad::HighPass,8600,1.0,0.5*v,0.042); r.tone(Q,9800,9400,0.06*v,0.02); }
        else if (id == "ohat") { r.noise(0.32,Biquad::HighPass,8400,1.0,0.5*v,0.28); }
        else if (id == "ltom") { r.tone(S,156,82,0.84*v,0.26); }
        else if (id == "htom") { r.tone(S,224,118,0.84*v,0.22); }
        else if (id == "rim") { r.tone(Q,1720,1640,0.22*v,0.05); r.noise(0.03,Biquad::BandPass,3100,5.0,0.42*v,0.024); }
        else if (id == "crash") { r.noise(1.1,Biquad::HighPass,6800,0.7,0.52*v,0.95); }
        else if (id == "shaker") { r.noise(0.11,Biquad::BandPass,6600,1.1,0.42*v,0.065,0.01); }
        else if (id == "conga") { r.tone(S,212,126,0.8*v,0.22); }
        else if (id == "bongo") { r.tone(S,312,188,0.75*v,0.15); }
        else if (id == "tamb") { r.noise(0.13,Biquad::HighPass,8800,1.0,0.36*v,0.075); }
        else if (id == "cow") { r.tone(Q,566,560,0.3*v,0.28); r.tone(Q,822,814,0.24*v,0.22); }
        else if (id == "clave") { r.tone(Q,2560,2440,0.32*v,0.05); }
    } else if (branch == "cr78") {
        if (id == "kick") { r.tone(S,118,52,0.82*v,0.22); r.noise(0.02,Biquad::LowPass,900,1.0,0.16*v,0.015); }
        else if (id == "snare") { r.noise(0.14,Biquad::BandPass,1750,1.0,0.5*v,0.09); r.tone(T,204,168,0.34*v,0.06); }
        else if (id == "clap") { r.noise(0.16,Biquad::BandPass,1220,1.6,0.42*v,0.12); }
        else if (id == "chat") { r.noise(0.09,Biquad::HighPass,9200,0.8,0.42*v,0.07); r.tone(Q,10400,9900,0.05*v,0.04); }
        else if (id == "ohat") { r.noise(0.62,Biquad::HighPass,9000,0.8,0.44*v,0.52); r.tone(Q,10200,9700,0.04*v,0.09); }
        else if (id == "ltom") { r.tone(S,148,76,0.72*v,0.24); }
        else if (id == "htom") { r.tone(S,212,108,0.72*v,0.2); }
        else if (id == "rim") { r.tone(Q,1980,1860,0.18*v,0.035); r.noise(0.03,Biquad::BandPass,3300,5.0,0.36*v,0.02); }
        else if (id == "crash") { r.noise(1.5,Biquad::HighPass,7600,0.6,0.44*v,1.3); }
        else if (id == "shaker") { r.noise(0.14,Biquad::BandPass,6800,1.0,0.4*v,0.09,0.012); }
        else if (id == "conga") { r.tone(S,198,118,0.76*v,0.23); r.noise(0.025,Biquad::BandPass,940,1.0,0.24*v,0.02); }
        else if (id == "bongo") { r.tone(S,298,178,0.72*v,0.16); r.noise(0.02,Biquad::BandPass,1320,1.0,0.22*v,0.015); }
        else if (id == "tamb") { r.noise(0.14,Biquad::HighPass,8600,1.0,0.32*v,0.08); }
        else if (id == "cow") { r.tone(Q,570,564,0.27*v,0.27); r.tone(Q,826,818,0.21*v,0.21); }
        else if (id == "clave") { r.tone(S,2420,2300,0.46*v,0.06); }
    } else {
        // Generic machine branch — 808, 909, 707, 606, 626, 505, 727,
        // electro, afrobeat, batucada, tombakdaf, persia, arabia.
        if (id == "kick") { r.tone(S,K.kickF,42,1.0*v,K.kickD); r.noise(0.03,Biquad::HighPass,4000,1.0,0.25*v,0.02); }
        else if (id == "snare") { r.noise(0.25,Biquad::BandPass,1900*br,0.7,0.7*v,0.13); r.tone(T,K.snareT,150,0.5*v,0.09); }
        else if (id == "clap") { r.clapBursts(br); }
        else if (id == "chat") { r.noise(0.08,Biquad::HighPass,8200*br,1.0,0.5*v,0.045*K.hatD); }
        else if (id == "ohat") { r.noise(0.5,Biquad::HighPass,8000*br,1.0,0.5*v,0.38*K.hatD); }
        else if (id == "ltom") { r.tone(S,150,72,0.8*v,0.28); }
        else if (id == "htom") { r.tone(S,215,105,0.8*v,0.24); }
        else if (id == "rim") {
            if (branch == "808") { r.tone(Q,540,520,0.28*v,0.09); r.tone(Q,805,790,0.22*v,0.07); }
            else { r.noise(0.05,Biquad::BandPass,3400,6.0,0.6*v,0.035); r.tone(Q,1720,1600,0.18*v,0.03); }
        }
        else if (id == "crash") { r.noise(1.4,Biquad::HighPass,6500*br,0.7,0.55*v,1.15); }
        else if (id == "shaker") { r.noise(0.12,Biquad::BandPass,6200*br,1.1,0.42*v,0.075,0.012); }
        else if (id == "conga") { r.tone(S,205*br,120,0.8*v,0.25); r.noise(0.03,Biquad::BandPass,1000,1.0,0.28*v,0.022); }
        else if (id == "bongo") { r.tone(S,305*br,185,0.75*v,0.17); r.noise(0.02,Biquad::BandPass,1400,1.0,0.24*v,0.016); }
        else if (id == "tamb") { r.noise(0.15,Biquad::HighPass,8400*br,1.0,0.36*v,0.085); r.noise(0.09,Biquad::BandPass,10600*br,3.0,0.24*v,0.05,0.004); }
        else if (id == "cow") { r.tone(Q,562,555,0.3*v,0.3); r.tone(Q,818,810,0.24*v,0.24); }
        else if (id == "clave") { r.tone(Q,2520*br,2400,0.32*v,0.055); r.noise(0.02,Biquad::BandPass,3500,4.0,0.26*v,0.014); }
    }

    out.samples = std::move(buf);
    out.numSamples = (int)std::ceil(r.maxEnd * sampleRate) + 1;
}

// ------------------------------------------------------------- click track
void renderClick(bool accent, double sampleRate, HitBuffer& out) {
    double freq = accent ? 1960.0 : 1240.0;
    double peak = accent ? 0.5 : 0.32;
    double dec = accent ? 0.055 : 0.04;
    int n = (int)std::ceil(0.09 * sampleRate) + 1;
    out.samples.assign(n, 0.0f);
    double phase = 0;
    for (int i = 0; i < n; ++i) {
        double t = (double)i / sampleRate;
        phase += 2.0 * M_PI * freq / sampleRate;
        double g = envGain(t, 0.002, peak, dec);
        out.samples[i] = (float)((std::sin(phase) >= 0 ? 1.0 : -1.0) * g);
    }
    out.numSamples = n;
}

// ------------------------------------------------------------ hall reverb
void HallReverb::prepare(double sampleRate, double decaySeconds) {
    sampleRate_ = sampleRate;
    double scale = sampleRate / 44100.0;
    const int combLens[4] = { 1116, 1188, 1277, 1356 };
    const int apLens[2] = { 556, 441 };
    for (int i = 0; i < 4; ++i) {
        combs_[i].buf.assign((size_t)(combLens[i] * scale) + 1, 0.0f);
        combs_[i].idx = 0;
        combs_[i].feedback = (float)std::pow(0.001, (combLens[i] * scale / sampleRate) / decaySeconds);
        combs_[i].damp = 0.2f;
        combs_[i].dampZ = 0;
    }
    for (int i = 0; i < 2; ++i) {
        allpasses_[i].buf.assign((size_t)(apLens[i] * scale) + 1, 0.0f);
        allpasses_[i].idx = 0;
        allpasses_[i].feedback = 0.5f;
    }
}

void HallReverb::reset() {
    for (auto& c : combs_) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.idx = 0; c.dampZ = 0; }
    for (auto& a : allpasses_) { std::fill(a.buf.begin(), a.buf.end(), 0.0f); a.idx = 0; }
}

float HallReverb::Comb::process(float x) {
    float out = buf[idx];
    dampZ = dampZ + damp * (out - dampZ);
    buf[idx] = x + feedback * dampZ;
    idx = (idx + 1) % (int)buf.size();
    return out;
}

float HallReverb::Allpass::process(float x) {
    float bufOut = buf[idx];
    float y = -x + bufOut;
    buf[idx] = x + feedback * bufOut;
    idx = (idx + 1) % (int)buf.size();
    return y;
}

void HallReverb::process(float* left, float* right, int numSamples) {
    for (int i = 0; i < numSamples; ++i) {
        float in = (left[i] + right[i]) * 0.5f;
        float s = 0;
        for (auto& c : combs_) s += c.process(in);
        s *= 0.25f;
        for (auto& a : allpasses_) s = a.process(s);
        left[i] = s * wet_;
        right[i] = s * wet_;
    }
}

// ---------------------------------------------------------- echo (delay)
void EchoDelay::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    size_t maxLen = (size_t)(sampleRate * 2.0) + 16;
    bufL_.assign(maxLen, 0.0f);
    bufR_.assign(maxLen, 0.0f);
    writeIdx_ = 0;
    delaySamples_ = (int)(sampleRate * 0.28);
    lpL_.set(3200, sampleRate);
    lpR_.set(3200, sampleRate);
}

void EchoDelay::reset() {
    std::fill(bufL_.begin(), bufL_.end(), 0.0f);
    std::fill(bufR_.begin(), bufR_.end(), 0.0f);
    writeIdx_ = 0;
}

void EchoDelay::setDelaySeconds(double seconds) {
    delaySamples_ = (int)std::max(1.0, seconds * sampleRate_);
}

void EchoDelay::process(float* left, float* right, int numSamples) {
    int n = (int)bufL_.size();
    int d = std::min(delaySamples_, n - 1);
    for (int i = 0; i < numSamples; ++i) {
        int readIdx = (writeIdx_ - d + n) % n;
        float dl = bufL_[readIdx], dr = bufR_[readIdx];
        float fbL = lpL_.process(dl), fbR = lpR_.process(dr);
        bufL_[writeIdx_] = left[i] + feedback_ * fbL;
        bufR_[writeIdx_] = right[i] + feedback_ * fbR;
        left[i] = dl;
        right[i] = dr;
        writeIdx_ = (writeIdx_ + 1) % n;
    }
}

// ------------------------------------------------------------- compressor
void MasterCompressor::prepare(double sampleRate) { sampleRate_ = sampleRate; }
void MasterCompressor::reset() { env_ = 0; }

void MasterCompressor::process(float* left, float* right, int numSamples) {
    const double thresholdDb = -14.0, ratio = 5.0;
    const double attackCoef = 1.0 - std::exp(-1.0 / (0.003 * sampleRate_));
    const double releaseCoef = 1.0 - std::exp(-1.0 / (0.18 * sampleRate_));
    for (int i = 0; i < numSamples; ++i) {
        double peak = std::max(std::fabs((double)left[i]), std::fabs((double)right[i]));
        double coef = peak > env_ ? attackCoef : releaseCoef;
        env_ += coef * (peak - env_);
        double envDb = 20.0 * std::log10(env_ + 1e-9);
        double gain = 1.0;
        if (envDb > thresholdDb) {
            double targetDb = thresholdDb + (envDb - thresholdDb) / ratio;
            gain = std::pow(10.0, (targetDb - envDb) / 20.0);
        }
        left[i] = (float)(left[i] * gain);
        right[i] = (float)(right[i] * gain);
    }
}

// -------------------------------------------------------------- WAV writer
namespace {
void appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((uint8_t)(v & 0xFF)); b.push_back((uint8_t)((v >> 8) & 0xFF));
}
void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v & 0xFF)); b.push_back((uint8_t)((v >> 8) & 0xFF));
    b.push_back((uint8_t)((v >> 16) & 0xFF)); b.push_back((uint8_t)((v >> 24) & 0xFF));
}
void appendStr(std::vector<uint8_t>& b, const char* s) {
    while (*s) b.push_back((uint8_t)*s++);
}
std::vector<uint8_t> encodeWav(const float* l, const float* r, int numSamples, int numChannels, int sampleRate) {
    std::vector<uint8_t> b;
    uint32_t dataBytes = (uint32_t)numSamples * numChannels * 2;
    appendStr(b, "RIFF"); appendU32(b, 36 + dataBytes); appendStr(b, "WAVE");
    appendStr(b, "fmt "); appendU32(b, 16); appendU16(b, 1);
    appendU16(b, (uint16_t)numChannels); appendU32(b, (uint32_t)sampleRate);
    appendU32(b, (uint32_t)(sampleRate * numChannels * 2));
    appendU16(b, (uint16_t)(numChannels * 2)); appendU16(b, 16);
    appendStr(b, "data"); appendU32(b, dataBytes);
    for (int i = 0; i < numSamples; ++i) {
        auto put = [&](float x) {
            int s = (int)std::lround(clampf(x, -1.0f, 1.0f) * 32767.0f);
            appendU16(b, (uint16_t)(int16_t)s);
        };
        put(l[i]);
        if (numChannels == 2) put(r[i]);
    }
    return b;
}
} // namespace

std::vector<uint8_t> encodeWavStereo(const float* left, const float* right, int numSamples, int sampleRate) {
    return encodeWav(left, right, numSamples, 2, sampleRate);
}
std::vector<uint8_t> encodeWavMono(const float* data, int numSamples, int sampleRate) {
    return encodeWav(data, nullptr, numSamples, 1, sampleRate);
}

} // namespace silktone
