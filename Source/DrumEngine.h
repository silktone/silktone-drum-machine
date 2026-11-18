// DrumEngine.h — The Silktone drum machine engine: sequencer, mixer, sampler,
// auto-chop, stem splitter and offline exports. Platform-independent C++17;
// the JUCE processor wraps this class. Faithful port of the behaviour in
// ~/workspace/your_files/drum-machine/drum-machine.html.
//
// Differences from the web app, by design:
//  - No browser / getUserMedia. Recording comes from the plugin's sidechain
//    input bus (Logic: sidechain the track to your UAD input) or, in the
//    Standalone build, from the selected audio device.
//  - The reverb is an algorithmic hall here (Freeverb-style). The JUCE
//    processor swaps in dsp::Convolution with the app's exact generated
//    noise-decay impulse when running in Logic / as VST3.
//  - Samples are kept in the plugin state (embedded in the saved chunk);
//    the web app keeps them session-only.
#pragma once

#include "DspCore.h"
#include "SilktoneData.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace silktone {

struct UserSample {
    std::vector<float> left, right;  // de-interleaved; right empty for mono
    double sampleRate = 48000;
    int chopBpm = 120;
    bool tempoLock = false;
    std::string name;
    bool valid = false;
};

struct Slice { double start = 0, end = 0; };  // seconds into the source buffer

struct Channel {
    int voiceIndex = 0;              // home voice (fixed)
    std::string instId;              // current instrument (scrolling changes it)
    std::string kitId;               // per-channel kit; empty = follow global kit
    std::array<int, 24> steps{};     // 0 off, 1 hit, 2 accent
    std::array<float, 24> vel{};     // per-step velocity (default .72)
    float vol = 0.85f;               // channel fader
    float tuneSemitones = 0;         // TUNE
    float pan = 0;                   // -50..50
    float tone = 0;                  // -50..50, tilt EQ (dark <-> bright)
    float saturation = 0;            // 0..100
    float sendRev = 0;               // auxR 0..100
    float sendDly = 0;               // auxD 0..100
    bool muted = false, solo = false;
    bool auxMuted = false, auxSolo = false;
    bool favorite = false;
    std::shared_ptr<UserSample> sample;  // loaded sample overrides the synth
    std::vector<Slice> slices;           // auto-chop slices of the loaded sample
};

struct LibraryEntry {
    std::string path, name, voiceId;  // voiceId empty when uncategorised
    bool favorite = false;
};

class DrumEngine {
public:
    static constexpr int kNumChannels = 30;
    static constexpr int kMaxSteps = 24;
    static constexpr int kMaxSlots = 48;

    // Loads the app's startup pattern ('house', like the web app's
    // loadPreset('house') on open) so the plugin is never empty.
    DrumEngine();

    void prepare(double sampleRate);
    void reset();

    // ------------------------------------------------------------ transport
    void play();        // start from step 0
    void resume();      // pick up at the held step
    void pause();       // hold at the current step
    void stop();        // stop and rewind to step 0
    void togglePlay();  // play -> pause -> resume -> play (one toggle + stop)
    bool isPlaying() const { return playing_; }
    bool isPaused() const { return paused_; }
    int currentStep() const { return curStep_; }
    void setHostBpm(double bpm);  // host tempo follows when sync is on

    double bpm() const { return bpm_; }
    void setBpm(double bpm);
    double swing() const { return swing_; }
    void setSwing(double pct);
    const std::string& swingInterval() const { return swingInterval_; }
    void setSwingInterval(const std::string& iv);  // "8" or "16"
    const std::string& sig() const { return sig_; }
    void setSig(const std::string& s);  // re-fits every row, beat by beat
    int steps() const;
    int kitIndex() const { return kitIndex_; }
    void setKit(int kitIndex);  // re-homes every channel to the new kit
    std::string kitId() const;
    double masterVol() const { return masterVol_; }
    void setMasterVol(double v);
    bool clickOn() const { return clickOn_; }
    void setClick(bool on);
    double reverbReturn() const { return reverbReturn_; }
    void setReverbReturn(double v);   // 0..1 (app default 0.15)
    double delayReturn() const { return delayReturn_; }
    void setDelayReturn(double v);    // 0..1 (app default 0.0)
    bool syncToHost() const { return syncToHost_; }
    void setSyncToHost(bool on);

    // ------------------------------------------------------------- pattern
    void loadPreset(int presetIndex);
    void loadPercLoop(int loopIndex);  // layers over the current pattern
    void clearRow(int ch);
    void clearAll();
    void copyRow(int ch);
    void pasteRow(int ch);
    bool hasClipboard() const { return clipboardValid_; }
    void setStep(int ch, int step, int value);
    void applySwingMachine(int machineIndex);
    void setStepVelocity(int ch, int step, float v);
    float stepVelocity(int ch, int step) const;

    // ------------------------------------------------------------ channels
    Channel& channel(int i) { return channels_[i]; }
    const Channel& channel(int i) const { return channels_[i]; }
    bool isChannelAudible(int ch) const;
    bool anyChannelSolo() const;
    bool anyAuxSolo() const;
    void scrollChannelSound(int ch, int dir);  // family scroll, whole library
    std::string channelSoundName(int ch) const;  // e.g. "909 Bass Drum"
    void setChannelKit(int ch, const std::string& kitId);
    void triggerChannel(int ch, double velocity);  // pad / MIDI note-on
    float channelPeak(int ch) const { return channelPeaks_[ch]; }

    // --------------------------------------------------------------- audio
    // sideL/sideR: record-input bus (sidechain) or nullptr.
    void processBlock(float* outL, float* outR, int numSamples,
                      const float* sideL, const float* sideR);

    // ------------------------------------------------------------- sampler
    // Minimal WAV decoder (PCM 16/24/32-bit int + IEEE float). The JUCE
    // processor decodes every other format and hands PCM to setSample().
    bool loadSampleWav(int ch, const uint8_t* bytes, size_t size, const std::string& name);
    void setSample(int ch, std::shared_ptr<UserSample> sample);
    void clearSample(int ch);
    void setSampleTempoLock(int ch, bool on, int chopBpm);
    // Auto-chop a buffer into slices. mode "transient" | "meter".
    static std::vector<Slice> detectSlices(const float* data, int numSamples, int numChannels,
                                           double sampleRate, const std::string& sensitivity);
    static std::vector<Slice> gridSlices(double duration, double bpm, const std::string& sig,
                                         const std::string& division);
    static std::vector<Slice> quantizeSlices(std::vector<Slice> slices, double bpm,
                                             const std::string& sig, const std::string& division,
                                             double duration);
    std::vector<Slice> autoChop(int ch, const std::string& mode, const std::string& division,
                                const std::string& sensitivity, bool locked);
    void assignSliceToVoice(int ch, int sliceIndex, int voiceIndex);

    // ----------------------------------------------------- library scan
    static std::string categorizeFilename(const std::string& filename);  // voiceForFilename
    std::vector<LibraryEntry> scanFolder(const std::string& path);
    static std::vector<LibraryEntry> searchLibrary(const std::vector<LibraryEntry>& lib,
                                                   const std::string& query);

    // ------------------------------------------------------- stem splitter
    // Frequency-band split, exactly as the app's stemGraph — NOT AI
    // separation. stages defaults to 2.
    struct StemAudio { std::vector<float> l, r; };
    struct SplitStems { StemAudio low, mid, perc, high, center, side; };
    static SplitStems splitStems(const float* l, const float* r, int numSamples,
                                 double sampleRate, int stages = 2);

    // -------------------------------------------------------------- export
    std::vector<uint8_t> buildMidi() const;  // SMF, port of buildMidiBlob
    // Offline render of the pattern, `bars` bars, dry (sends off, no click),
    // master 0.85 — like the app's renderWavBlob. Returns stereo buffers
    // at the engine sample rate.
    void renderLoop(int bars, std::vector<float>& outL, std::vector<float>& outR);
    // Per-voice dry stems (one render per sounding channel).
    void renderStems(std::vector<StemAudio>& stems, std::vector<std::string>& names);

    // ------------------------------------------------------------ recording
    void startRecording();
    // Returns the recorded buffer as a sample (nullptr when nothing recorded).
    std::shared_ptr<UserSample> stopRecording(const std::string& name);
    bool isRecording() const { return recording_; }

    // ------------------------------------------------------------ MIDI out
    // When enabled, the sequencer emits the pattern as MIDI (channel 10)
    // alongside the audio, so Logic can record it or drive another drum.
    struct MidiOutEvent { int sampleOffset; int note; int velocity; bool noteOn; };
    void setMidiOut(bool on) { midiOutEnabled_ = on; }
    bool midiOutEnabled() const { return midiOutEnabled_; }
    std::vector<MidiOutEvent> takeMidiOutEvents();

    double sampleRate() const { return sampleRate_; }

    // Pattern helpers, exposed for the tests and the editor.
    static std::vector<int> parsePat(const std::string& str, int len);
    static std::vector<int> remapBar(const std::vector<int>& steps, const std::vector<float>& vel,
                                     const std::string& fromSig, const std::string& toSig,
                                     std::vector<float>& outVel);
    double swingDelaySeconds(int step) const;
    double stepDurationSeconds() const { return 60.0 / bpm_ / 4.0; }

private:
    KitParams kitParamsFor(const std::string& kitId) const;
    void triggerHit(int ch, double velocity, int64_t atSample, bool click, bool accent);
    void triggerClick(int64_t atSample, bool accent);
    void createSlot(int ch, double velocity, int64_t atSample, bool click, bool accent);
    void scheduleStep(int step);
    int sigSteps(const std::string& s) const;
    bool isGroupStart(int step) const;

    double sampleRate_ = 48000;
    double bpm_ = 120, swing_ = 50;
    std::string swingInterval_ = "16";
    std::string sig_ = "4/4";
    int kitIndex_ = 0;  // index into kKitOrder ("808")
    double masterVol_ = 0.8;
    bool clickOn_ = false;
    double reverbReturn_ = 0.15;
    double delayReturn_ = 0.0;
    bool syncToHost_ = true;

    std::array<Channel, kNumChannels> channels_;
    std::array<float, kNumChannels> channelPeaks_{};

    bool playing_ = false, paused_ = false;
    int curStep_ = 0;
    int64_t samplePos_ = 0;       // absolute sample clock
    int64_t nextStepSample_ = 0;  // absolute sample of the next step boundary

    struct PendingTrigger { int64_t atSample; int channel; double velocity; bool click; bool accent; };
    std::vector<PendingTrigger> pending_;

    struct Slot {
        bool active = false;
        bool isSample = false;
        bool isClick = false;
        HitBuffer hit;                       // synth hit (mono, saturated)
        std::shared_ptr<UserSample> sample;  // sample hit
        double position = 0;                 // fractional position in hit/sample
        double rate = 1.0;
        float panL = 1, panR = 1;
        float sendRev = 0, sendDly = 0;
        float vol = 1;
        int channel = 0;
        int startOffset = 0;  // samples into the block where the hit starts
    };
    std::array<Slot, kMaxSlots> slots_;

    NoiseBuffer noise_;
    HallReverb hall_;
    EchoDelay echo_;
    MasterCompressor comp_;
    HitBuffer clickHit_[2];  // [0] normal, [1] accent

    std::array<int, kMaxSteps> clipboardSteps_{};
    std::array<float, kMaxSteps> clipboardVel_{};
    bool clipboardValid_ = false;

    bool recording_ = false;
    std::vector<float> recordL_, recordR_;
    std::vector<float> revBus_, dlyBus_;  // per-block scratch (no allocation in processBlock)
    int64_t blockEnd_ = 0;  // end of the block currently being processed
    bool midiOutEnabled_ = false;
    std::vector<MidiOutEvent> midiOutEvents_;
    struct PendingMidiOff { int64_t atSample; int note; };
    std::vector<PendingMidiOff> midiOffPending_;

    // Offline-render bookkeeping.
    bool rendering_ = false;
};

} // namespace silktone
