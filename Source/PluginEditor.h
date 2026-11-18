// PluginEditor.h — the plugin UI: transport bar + 7 tabs (Sequencer | Pads |
// Mixer | Sampler | Library | Stems | Export), 70s Silktone look.
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"

class SilktoneDrumMachineEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SilktoneDrumMachineEditor(SilktoneDrumMachineAudioProcessor& processor);
    ~SilktoneDrumMachineEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showInfo();
    void tapTempo();

    SilktoneDrumMachineAudioProcessor& processor_;
    SilktoneLookAndFeel lookAndFeel_;

    // Top transport bar.
    juce::TextButton playButton_{ "▶" };
    juce::TextButton stopButton_{ "■" };
    juce::TextButton tapButton_{ "TAP" };
    juce::Slider bpmSlider_;
    juce::Slider swingSlider_;
    juce::ComboBox swingIntervalBox_;
    juce::ComboBox sigBox_;
    juce::ComboBox kitBox_;
    juce::ComboBox presetBox_;
    juce::Slider masterSlider_;
    juce::ToggleButton clickButton_{ "CLICK" };
    juce::TextButton infoButton_{ "?" };

    juce::TabbedComponent tabs_{ juce::TabbedButtonBar::TabsAtTop };

    // Parameter attachments (must outlive the controls).
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> swingIntervalAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sigAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> kitAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clickAttachment_;

    std::vector<double> tapTimes_;
    double lastTapTime_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilktoneDrumMachineEditor)
};
