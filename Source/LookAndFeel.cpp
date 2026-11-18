// LookAndFeel.cpp
#include "LookAndFeel.h"

SilktoneLookAndFeel::SilktoneLookAndFeel() {
    setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1c120a));
    setColour(juce::Slider::trackColourId, amber());
    setColour(juce::Slider::thumbColourId, amberBright());
    setColour(juce::Slider::rotarySliderFillColourId, amber());
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff5a4630));
    setColour(juce::Slider::textBoxTextColourId, cream());
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1c120a));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff5a4630));
    setColour(juce::ComboBox::backgroundColourId, woodPanel());
    setColour(juce::ComboBox::textColourId, cream());
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff5a4630));
    setColour(juce::ComboBox::buttonColourId, amber());
    setColour(juce::ComboBox::arrowColourId, amber());
    setColour(juce::TextButton::buttonColourId, woodPanel2());
    setColour(juce::TextButton::buttonOnColourId, amber());
    setColour(juce::TextButton::textColourOffId, cream());
    setColour(juce::TextButton::textColourOnId, woodDark());
    setColour(juce::ToggleButton::textColourId, cream());
    setColour(juce::Label::textColourId, cream());
    setColour(juce::PopupMenu::backgroundColourId, woodPanel());
    setColour(juce::PopupMenu::textColourId, cream());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, amber());
    setColour(juce::PopupMenu::highlightedTextColourId, woodDark());
    setColour(juce::TabbedComponent::outlineColourId, juce::Colour(0xff5a4630));
    setColour(juce::TabbedButtonBar::tabTextColourId, creamDim());
    setColour(juce::TabbedButtonBar::frontTextColourId, amberBright());
    setColour(juce::ListBox::backgroundColourId, woodDark());
    setColour(juce::ListBox::textColourId, cream());
    setColour(juce::ListBox::outlineColourId, juce::Colour(0xff5a4630));
    setColour(juce::TextEditor::backgroundColourId, woodDark());
    setColour(juce::TextEditor::textColourId, cream());
    setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff5a4630));
    setColour(juce::TextEditor::focusedOutlineColourId, amber());
    setColour(juce::ScrollBar::backgroundColourId, woodDark());
    setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff5a4630));
}

void SilktoneLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle,
                                           float rotaryEndAngle, juce::Slider&) {
    auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height)
                      .reduced(2.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto centre = bounds.getCentre();

    // Track arc.
    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                      rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xff5a4630));
    g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
    // Value arc (amber).
    juce::Path valueArc;
    valueArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                           rotaryStartAngle, toAngle, true);
    g.setColour(amber());
    g.strokePath(valueArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    // Knob body.
    auto r = radius - 6.0f;
    juce::ColourGradient grad(amberBright(), centre - juce::Point<float>(r * 0.4f, r * 0.4f),
                              woodPanel2(), centre + juce::Point<float>(r * 0.5f, r * 0.5f), false);
    g.setGradientFill(grad);
    g.fillEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
    g.setColour(juce::Colour(0x661c120a));
    g.drawEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 1.0f);
    // Pointer line.
    auto pointer = centre.getPointOnCircumference(r - 2.0f, toAngle);
    g.setColour(woodDark());
    g.drawLine(centre.x, centre.y, pointer.x, pointer.y, 2.0f);
}

void SilktoneLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float minSliderPos, float maxSliderPos,
                                           const juce::Slider::SliderStyle style, juce::Slider& slider) {
    if (style == juce::Slider::LinearVertical) {
        auto trackW = 6.0f;
        auto cx = x + width / 2.0f;
        g.setColour(juce::Colour(0xff1c120a));
        g.fillRoundedRectangle(cx - trackW / 2, (float)y, trackW, (float)height, 3.0f);
        g.setColour(amber());
        g.fillRoundedRectangle(cx - trackW / 2, sliderPos, trackW, maxSliderPos - sliderPos, 3.0f);
        // Thumb: a wide amber cap with a cream centre line.
        auto thumbH = 14.0f;
        auto ty = sliderPos - thumbH / 2.0f;
        g.setColour(amberBright());
        g.fillRoundedRectangle(cx - 15.0f, ty, 30.0f, thumbH, 3.0f);
        g.setColour(woodDark());
        g.fillRect(cx - 15.0f, ty + thumbH / 2.0f - 1.0f, 30.0f, 2.0f);
    } else {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                         minSliderPos, maxSliderPos, style, slider);
    }
}

void SilktoneLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool shouldDrawButtonAsDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    bool on = button.getToggleState();
    g.setColour(on ? amber() : woodPanel2());
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(on ? woodDark() : cream());
    g.drawText(button.getButtonText(), bounds, juce::Justification::centred, false);
    juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

void SilktoneLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    auto colour = backgroundColour;
    if (shouldDrawButtonAsDown) colour = colour.darker(0.2f);
    else if (shouldDrawButtonAsHighlighted) colour = colour.brighter(0.15f);
    g.setColour(colour);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(juce::Colour(0xff5a4630));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

void SilktoneLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                       int buttonX, int buttonY, int buttonW, int buttonH,
                                       juce::ComboBox& box) {
    juce::ignoreUnused(isButtonDown);
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    // Arrow.
    juce::Path arrow;
    auto ax = (float)(buttonX + buttonW / 2);
    auto ay = (float)(buttonY + buttonH / 2);
    arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

juce::Font SilktoneLookAndFeel::getLabelFont(juce::Label&) {
    return juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain);
}

void SilktoneLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    g.setColour(label.findColour(juce::Label::textColourId));
    g.setFont(getLabelFont(label));
    g.drawText(label.getText(), label.getLocalBounds(),
               label.getJustificationType(), true);
}

void SilktoneLookAndFeel::drawTabButton(juce::TabBarButton& button, juce::Graphics& g,
                                        bool isMouseOver, bool isMouseDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f, 1.0f);
    bool active = button.isFrontTab();
    g.setColour(active ? amber() : woodPanel2());
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(active ? woodDark() : creamDim());
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    g.drawText(button.getButtonText(), bounds, juce::Justification::centred, false);
    juce::ignoreUnused(isMouseOver, isMouseDown);
}
