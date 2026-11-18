// LookAndFeel.h — Silktone 70s look: browns, yellows, wood, amber.
// Matches the web app's palette (wood-panelled 70s cabinet, amber rings,
// cream labels) in a Logic-style channel-strip layout.
#pragma once

#include <JuceHeader.h>

class SilktoneLookAndFeel : public juce::LookAndFeel_V4 {
public:
    SilktoneLookAndFeel();

    static juce::Colour woodDark()     { return juce::Colour(0xff2b1d12); }
    static juce::Colour woodPanel()    { return juce::Colour(0xff3a2a1a); }
    static juce::Colour woodPanel2()   { return juce::Colour(0xff4a3520); }
    static juce::Colour amber()        { return juce::Colour(0xfff5a623); }
    static juce::Colour amberBright()  { return juce::Colour(0xfff5c542); }
    static juce::Colour orange()       { return juce::Colour(0xffff6a1a); }
    static juce::Colour cream()        { return juce::Colour(0xfff5e6c8); }
    static juce::Colour creamDim()     { return juce::Colour(0xffc9b18a); }
    static juce::Colour red()          { return juce::Colour(0xffe83a2a); }
    static juce::Colour green()        { return juce::Colour(0xff7bc47f); }

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    juce::Font getLabelFont(juce::Label&) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
};
