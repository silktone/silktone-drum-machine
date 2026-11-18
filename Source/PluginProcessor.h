// PluginProcessor.h — JUCE wrapper around silktone::DrumEngine.
// AU (.component) + VST3 (.vst3) + Standalone, no installer.
// The audio path is the engine (DspCore + DrumEngine); JUCE provides the
// plugin shell, host parameters, state, file I/O and the editor.
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <JuceHeader.h>
#include "DrumEngine.h"

class SilktoneDrumMachineAudioProcessor : public juce::AudioProcessor {
public:
    SilktoneDrumMachineAudioProcessor();
    ~SilktoneDrumMachineAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Silktone Drum Machine"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    silktone::DrumEngine& getEngine() { return engine_; }
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts_; }

    // ---- UI-facing actions (called from the editor, message thread) -------
    void loadPreset(int presetIndex);
    int getPatternIndex() const { return patternIndex_; }
    void setPatternIndex(int i) { patternIndex_ = i; }

    void exportMidi(const juce::File& file);
    void exportLoopWav(const juce::File& file, int bars = 4);
    void exportStemsZip(const juce::File& file);

    void loadSampleOntoChannel(int channel, const juce::File& file);
    void loadSampleOntoChannel(int channel, const juce::String& name,
                               const juce::AudioBuffer<float>& buffer, double sampleRate);
    void clearSampleOnChannel(int channel);

    void scanLibraryFolder(const juce::File& folder);
    const std::vector<silktone::LibraryEntry>& getLibrary() const { return library_; }
    void toggleLibraryFavorite(const juce::String& path);

    // Stem splitter: load a file, split it, keep the 6 stems for preview/send.
    bool loadStemSource(const juce::File& file);
    const std::vector<silktone::DrumEngine::StemAudio>& getSplitStems() const { return splitStems_; }
    const juce::String& getStemSourceName() const { return stemSourceName_; }
    bool hasStemSource() const;
    juce::String stemSourceName() const;
    void previewStem(int stemIndex);
    void sendStemToPad(int stemIndex, int voiceIndex);
    void exportStemWav(int stemIndex, const juce::File& file);

    // Recording from the sidechain / input bus.
    void startRecording(int targetChannel);
    // Returns the recorded sample (also assigned to the target channel).
    std::shared_ptr<silktone::UserSample> stopRecording();
    bool isRecording() const { return engine_.isRecording(); }
    float getInputLevel() const { return inputLevel_.load(); }
    juce::String getRecordSourceDescription() const;

    // Preview player (stem audition): mixes a buffer over the output.
    void startPreview(const juce::AudioBuffer<float>& buffer);
    void stopPreview();
    bool isPreviewing() const { return previewing_; }

    // Transport helpers for the editor.
    void togglePlay() { engine_.togglePlay(); }
    void stopTransport() { engine_.stop(); }

private:
    void syncParametersToEngine(bool force = false);
    void applyChannelStateFromValueTree(const juce::ValueTree& state);
    juce::String stepsToString(int channel) const;
    void stepsFromString(int channel, const juce::String& s);
    void writeSampleToWavBytes(const silktone::UserSample& sample, juce::MemoryBlock& out) const;

    silktone::DrumEngine engine_;
    juce::AudioProcessorValueTreeState apvts_;

    // Cached parameter values for change detection in processBlock.
    struct ParamCache {
        float bpm = -1, swing = -1, masterVol = -1, revReturn = -1, dlyReturn = -1;
        int swingInterval = -1, sig = -1, kit = -1;
        bool click = false, syncHost = false, followHost = false, midiOut = false;
        bool clickSet = false, syncHostSet = false, followHostSet = false, midiOutSet = false;
        std::array<float, 30> vol, tune, pan, tone, sat, sendRev, sendDly;
        ParamCache() { vol.fill(-1); tune.fill(-999); pan.fill(-999); tone.fill(-999); sat.fill(-1); sendRev.fill(-1); sendDly.fill(-1); }
    };
    ParamCache cache_;

    int patternIndex_ = -1;  // -1 = no preset loaded / edited pattern
    bool hostWasPlaying_ = false;

    std::vector<silktone::LibraryEntry> library_;
    std::vector<silktone::DrumEngine::StemAudio> splitStems_;
    juce::String stemSourceName_;
    double stemSourceSampleRate_ = 48000;

    int recordTargetChannel_ = 0;

    // Preview player state (audio thread reads, message thread writes).
    juce::AudioBuffer<float> previewBuffer_;
    std::atomic<int> previewPos_{ 0 };
    std::atomic<bool> previewing_{ false };
    std::atomic<float> inputLevel_{ 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilktoneDrumMachineAudioProcessor)
};
