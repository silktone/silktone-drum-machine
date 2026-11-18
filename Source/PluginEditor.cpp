// PluginEditor.cpp — transport bar + 7 tabs. Step cells, pads, mixer strips,
// sampler, library, stems and export all talk to DrumEngine through the
// processor; automatable values go through the APVTS attachments.
#include <map>
#include "PluginEditor.h"

using namespace silktone;

namespace {

juce::StringArray voiceNames() {
    juce::StringArray names;
    for (auto& v : kVoices) names.add(v.name);
    return names;
}
// Library entries carry a voice id ("kick"); show the human-readable voice name.
std::string voiceDisplayName(const std::string& voiceId) {
    for (auto& v : kVoices)
        if (voiceId == v.id) return v.name;
    return voiceId;  // empty when uncategorised
}
juce::StringArray kitNames() {
    juce::StringArray names;
    for (auto id : kKitOrder)
        for (auto& k : kKits)
            if (id == k.id) { names.add(k.name); break; }
    return names;
}
juce::StringArray sigNames() {
    juce::StringArray names;
    for (auto& s : kSigs) names.add(s.name);
    return names;
}
juce::StringArray presetNames() {
    juce::StringArray names;
    for (auto& p : kPresets) names.add(p.name);
    return names;
}

// A tab that the editor's timer can refresh (playhead, meters, state).
class RefreshableTab : public juce::Component {
public:
    virtual void refresh() {}
    virtual ~RefreshableTab() = default;
};

} // namespace

// ================================================================ SEQUENCER
namespace {

class StepCell : public juce::Component {
public:
    StepCell(DrumEngine& engine, int channel, int step)
        : engine_(engine), channel_(channel), step_(step) {}

    void paint(juce::Graphics& g) override {
        auto& c = engine_.channel(channel_);
        int state = step_ < engine_.steps() ? c.steps[step_] : 0;
        auto bounds = getLocalBounds().toFloat().reduced(1.5f);
        bool isPlayhead = engine_.isPlaying() && step_ == engine_.currentStep();
        bool isBeat = step_ % 4 == 0;

        juce::Colour fill = juce::Colour(0xff241a10);
        if (state == 1) fill = SilktoneLookAndFeel::amber();
        else if (state == 2) fill = SilktoneLookAndFeel::orange();
        if (isPlayhead) fill = fill.brighter(0.35f);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 3.0f);
        if (isBeat) {
            g.setColour(juce::Colour(0xff5a4630));
            g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        }
        if (state == 2) {  // accent: a dot in the middle
            g.setColour(SilktoneLookAndFeel::woodDark());
            g.fillEllipse(bounds.getCentreX() - 2.5f, bounds.getCentreY() - 2.5f, 5.0f, 5.0f);
        }
    }

    void mouseDown(const juce::MouseEvent&) override {
        auto& c = engine_.channel(channel_);
        if (step_ >= engine_.steps()) return;
        int next = (c.steps[step_] + 1) % 3;  // 0 -> hit -> accent -> off
        c.steps[step_] = next;
        c.vel[step_] = next == 2 ? 1.0f : 0.72f;
        if (next > 0) engine_.triggerChannel(channel_, next == 2 ? 1.0f : 0.72f);
        repaint();
    }

private:
    DrumEngine& engine_;
    int channel_, step_;
};

class SequencerGrid : public RefreshableTab {
public:
    explicit SequencerGrid(SilktoneDrumMachineAudioProcessor& p) : processor_(p) {
        auto& engine = processor_.getEngine();
        for (int ch = 0; ch < 30; ++ch) {
            auto* name = new juce::TextButton(kVoices[ch].name);
            name->setTooltip("Tap to hear " + juce::String(kVoices[ch].name));
            name->onClick = [this, ch] { processor_.getEngine().triggerChannel(ch, 1.0f); };
            addAndMakeVisible(name);
            nameButtons_.add(name);

            auto* mute = new juce::ToggleButton("M");
            mute->setTooltip("Mute " + juce::String(kVoices[ch].name));
            mute->onClick = [this, ch, mute] { processor_.getEngine().channel(ch).muted = mute->getToggleState(); };
            addAndMakeVisible(mute);
            muteButtons_.add(mute);

            auto* solo = new juce::ToggleButton("S");
            solo->setTooltip("Solo " + juce::String(kVoices[ch].name));
            solo->onClick = [this, ch, solo] { processor_.getEngine().channel(ch).solo = solo->getToggleState(); };
            addAndMakeVisible(solo);
            soloButtons_.add(solo);

            auto* prev = new juce::TextButton("◀");
            prev->setTooltip("Previous sound in this family");
            prev->onClick = [this, ch] { processor_.getEngine().scrollChannelSound(ch, -1); };
            addAndMakeVisible(prev);
            prevButtons_.add(prev);

            auto* next = new juce::TextButton("▶");
            next->setTooltip("Next sound in this family");
            next->onClick = [this, ch] { processor_.getEngine().scrollChannelSound(ch, 1); };
            addAndMakeVisible(next);
            nextButtons_.add(next);

            auto* clr = new juce::TextButton("CLR");
            clr->setTooltip("Clear this row");
            clr->onClick = [this, ch] { processor_.getEngine().clearRow(ch); };
            addAndMakeVisible(clr);
            clearButtons_.add(clr);

            for (int st = 0; st < 24; ++st) {
                auto* cell = new StepCell(engine, ch, st);
                addAndMakeVisible(cell);
                cells_.add(cell);
            }
        }
        // Fixed grid size: label column + 24 steps.
        setSize(150 + 8 + 24 * 26 + 8, 30 * 22 + 8);
        layoutRows();
    }

    void resized() override { layoutRows(); }

    void refresh() override {
        auto& engine = processor_.getEngine();
        int step = engine.currentStep();
        bool playing = engine.isPlaying();
        if (step != lastStep_ || playing != lastPlaying_ || engine.steps() != lastSteps_) {
            lastStep_ = step;
            lastPlaying_ = playing;
            lastSteps_ = engine.steps();
            layoutRows();
            repaint();
        }
        // Keep M/S buttons in step with the engine (they can change from state loads).
        for (int ch = 0; ch < 30; ++ch) {
            auto& c = engine.channel(ch);
            if (muteButtons_[ch]->getToggleState() != c.muted)
                muteButtons_[ch]->setToggleState(c.muted, juce::dontSendNotification);
            if (soloButtons_[ch]->getToggleState() != c.solo)
                soloButtons_[ch]->setToggleState(c.solo, juce::dontSendNotification);
        }
    }

private:
    void layoutRows() {
        auto& engine = processor_.getEngine();
        int steps = engine.steps();
        int cellW = 26, rowH = 22;
        int x = 150 + 8;
        int y = 4;
        for (int ch = 0; ch < 30; ++ch) {
            nameButtons_[ch]->setBounds(0, y, 108, rowH - 2);
            muteButtons_[ch]->setBounds(110, y, 22, rowH - 2);
            soloButtons_[ch]->setBounds(132, y, 22, rowH - 2);
            prevButtons_[ch]->setBounds(156, y, 16, rowH - 2);
            nextButtons_[ch]->setBounds(172, y, 16, rowH - 2);
            clearButtons_[ch]->setBounds(190, y, 30, rowH - 2);
            for (int st = 0; st < 24; ++st) {
                cells_[ch * 24 + st]->setBounds(x + st * cellW, y, cellW - 2, rowH - 2);
                cells_[ch * 24 + st]->setVisible(st < steps);
            }
            y += rowH;
        }
    }

    SilktoneDrumMachineAudioProcessor& processor_;
    juce::OwnedArray<juce::TextButton> nameButtons_, prevButtons_, nextButtons_, clearButtons_;
    juce::OwnedArray<juce::ToggleButton> muteButtons_, soloButtons_;
    juce::OwnedArray<StepCell> cells_;
    int lastStep_ = -1, lastSteps_ = -1;
    bool lastPlaying_ = false;
};

// Scrollable wrapper: shows any fixed-size RefreshableTab inside a Viewport.
class ScrollWrapper : public RefreshableTab {
public:
    explicit ScrollWrapper(RefreshableTab* content) : content_(content) {
        viewport_.setViewedComponent(content, true);  // viewport owns the content
        viewport_.setScrollBarsShown(true, true);
        addAndMakeVisible(viewport_);
    }
    void resized() override { viewport_.setBounds(getLocalBounds()); }
    void refresh() override { if (content_) content_->refresh(); }

private:
    juce::Viewport viewport_;
    RefreshableTab* content_ = nullptr;
};

class SequencerTab : public RefreshableTab {
public:
    explicit SequencerTab(SilktoneDrumMachineAudioProcessor& p) : processor_(p) {
        grid_ = new SequencerGrid(processor_);
        wrapper_ = new ScrollWrapper(grid_);
        addAndMakeVisible(wrapper_);
        copyButton_.setButtonText("Copy row");
        copyButton_.onClick = [this] { processor_.getEngine().copyRow(selectedRow_); };
        addAndMakeVisible(copyButton_);
        pasteButton_.setButtonText("Paste row");
        pasteButton_.onClick = [this] { processor_.getEngine().pasteRow(selectedRow_); };
        addAndMakeVisible(pasteButton_);
        clearAllButton_.setButtonText("Clear all");
        clearAllButton_.onClick = [this] {
            for (int ch = 0; ch < 30; ++ch) processor_.getEngine().clearRow(ch);
        };
        addAndMakeVisible(clearAllButton_);
        rowLabel_.setText("Row:", juce::dontSendNotification);
        addAndMakeVisible(rowLabel_);
        rowBox_.addItemList(voiceNames(), 1);
        rowBox_.setSelectedId(1, juce::dontSendNotification);
        rowBox_.onChange = [this] { selectedRow_ = rowBox_.getSelectedId() - 1; };
        addAndMakeVisible(rowBox_);
        swingMachineLabel_.setText("Swing machine:", juce::dontSendNotification);
        addAndMakeVisible(swingMachineLabel_);
        for (int i = 0; i < kNumSwingMachines; ++i)
            swingMachineBox_.addItem(juce::String(kSwingMachines[i].name) + " — " +
                                         juce::String(kSwingMachines[i].percent) + "%",
                                     i + 1);
        swingMachineBox_.setTextWhenNothingSelected("Swing machine…");
        swingMachineBox_.onChange = [this] {
            int idx = swingMachineBox_.getSelectedId() - 1;
            if (idx < 0) return;
            processor_.getEngine().applySwingMachine(idx);
            float pct = (float)kSwingMachines[idx].percent;
            if (auto* p = processor_.getAPVTS().getParameter("swing"))
                p->setValueNotifyingHost((pct - 50.0f) / 25.0f);
        };
        addAndMakeVisible(swingMachineBox_);
    }

    void resized() override {
        copyButton_.setBounds(8, 4, 90, 22);
        pasteButton_.setBounds(102, 4, 90, 22);
        clearAllButton_.setBounds(196, 4, 90, 22);
        rowLabel_.setBounds(300, 4, 40, 22);
        rowBox_.setBounds(340, 4, 180, 22);
        swingMachineLabel_.setBounds(536, 4, 110, 22);
        swingMachineBox_.setBounds(646, 4, 200, 22);
        wrapper_->setBounds(0, 30, getWidth(), getHeight() - 30);
    }

    void refresh() override { if (grid_) grid_->refresh(); }

private:
    SilktoneDrumMachineAudioProcessor& processor_;
    SequencerGrid* grid_ = nullptr;
    ScrollWrapper* wrapper_ = nullptr;
    juce::TextButton copyButton_, pasteButton_, clearAllButton_;
    juce::Label rowLabel_, swingMachineLabel_;
    juce::ComboBox rowBox_, swingMachineBox_;
    int selectedRow_ = 0;
};

// ==================================================================== PADS
class PadButton : public juce::Component {
public:
    PadButton(DrumEngine& engine, int channel) : engine_(engine), channel_(channel) {}

    void paint(juce::Graphics& g) override {
        auto& c = engine_.channel(channel_);
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        bool lit = litUntil_ > juce::Time::getMillisecondCounter();
        juce::Colour fill = lit ? SilktoneLookAndFeel::amber() : SilktoneLookAndFeel::woodPanel2();
        if (c.muted) fill = fill.darker(0.5f);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(SilktoneLookAndFeel::cream());
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        g.drawText(kVoices[channel_].name, bounds.reduced(4.0f, 4.0f),
                   juce::Justification::topLeft, true);
        g.setFont(juce::Font(11.0f));
        g.setColour(SilktoneLookAndFeel::creamDim());
        g.drawText(juce::String(c.kitId) + (c.sample && c.sample->valid ? " • SMP" : ""),
                   bounds.reduced(4.0f, 4.0f), juce::Justification::bottomLeft, false);
        if (c.favorite) {
            g.setColour(SilktoneLookAndFeel::amberBright());
            g.drawText("★", bounds.reduced(4.0f), juce::Justification::topRight, false);
        }
        if (c.solo) {
            g.setColour(SilktoneLookAndFeel::green());
            g.drawText("S", bounds.reduced(4.0f), juce::Justification::bottomRight, false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        float vel = 1.0f - (float)e.y / (float)getHeight() * 0.5f;
        engine_.triggerChannel(channel_, juce::jlimit(0.4f, 1.0f, vel));
        litUntil_ = juce::Time::getMillisecondCounter() + 120;
        repaint();
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            menu.addItem(1, engine_.channel(channel_).favorite ? "Remove ★ favorite" : "★ Favorite");
            menu.addItem(2, "Mute");
            menu.addItem(3, "Solo");
            menu.addItem(4, "Clear sample");
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                [this](int result) {
                    auto& c = engine_.channel(channel_);
                    if (result == 1) c.favorite = !c.favorite;
                    else if (result == 2) c.muted = !c.muted;
                    else if (result == 3) c.solo = !c.solo;
                    else if (result == 4) engine_.clearSample(channel_);
                    repaint();
                });
        }
    }

private:
    DrumEngine& engine_;
    int channel_;
    uint32_t litUntil_ = 0;
};

class PadDeckGrid : public RefreshableTab {
public:
    explicit PadDeckGrid(SilktoneDrumMachineAudioProcessor& p) : processor_(p) {
        for (int ch = 0; ch < 30; ++ch) {
            auto* pad = new PadButton(processor_.getEngine(), ch);
            addAndMakeVisible(pad);
            pads_.add(pad);
        }
        infoLabel_.setText("Tap a pad to play it. Tap low for a soft hit, high for a hard one. "
                           "Right-click a pad for mute / solo / favorite / clear sample.",
                           juce::dontSendNotification);
        infoLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(infoLabel_);
        // Fixed size: 6 columns x 5 rows of pads.
        setSize(6 * (168 + 8) + 8, 32 + 5 * (78 + 8) + 8);
        layoutPads();
    }

    void resized() override { layoutPads(); }

    void refresh() override { repaint(); }

private:
    void layoutPads() {
        int cols = 6, padW = 168, padH = 78, gap = 8;
        infoLabel_.setBounds(8, 4, getWidth() - 16, 22);
        int y = 32;
        for (int ch = 0; ch < 30; ++ch) {
            int col = ch % cols, row = ch / cols;
            pads_[ch]->setBounds(8 + col * (padW + gap), y + row * (padH + gap), padW, padH);
        }
    }

    SilktoneDrumMachineAudioProcessor& processor_;
    juce::OwnedArray<PadButton> pads_;
    juce::Label infoLabel_;
};

// =================================================================== MIXER
class LevelMeter : public juce::Component {
public:
    void setLevel(float v) { level_ = v; repaint(); }
    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff1c120a));
        g.fillRoundedRectangle(bounds, 2.0f);
        float h = bounds.getHeight() * juce::jlimit(0.0f, 1.0f, level_);
        g.setColour(level_ > 0.85f ? SilktoneLookAndFeel::red() : SilktoneLookAndFeel::green());
        g.fillRoundedRectangle(bounds.getX(), bounds.getBottom() - h, bounds.getWidth(), h, 2.0f);
    }
private:
    float level_ = 0.0f;
};

class ChannelStrip : public juce::Component {
public:
    ChannelStrip(SilktoneDrumMachineAudioProcessor& p, int channel)
        : processor_(p), channel_(channel) {
        juce::String prefix = "ch" + juce::String(channel) + "_";
        nameLabel_.setText(kVoices[channel].name, juce::dontSendNotification);
        nameLabel_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(nameLabel_);

        fader_.setSliderStyle(juce::Slider::LinearVertical);
        fader_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        fader_.setTooltip("Volume");
        addAndMakeVisible(fader_);
        volAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor_.getAPVTS(), prefix + "vol", fader_);

        auto makeKnobAttachment = [&](juce::Slider& s, const juce::String& id, const juce::String& tip) {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            s.setTooltip(tip);
            addAndMakeVisible(s);
            knobAttachments_.push_back(
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    processor_.getAPVTS(), prefix + id, s));
        };
        makeKnobAttachment(panKnob_, "pan", "Pan (L/R)");
        makeKnobAttachment(toneKnob_, "tone", "Tone (tilt EQ)");
        makeKnobAttachment(satKnob_, "sat", "Saturation");
        makeKnobAttachment(revKnob_, "sendRev", "Send to Reverb");
        makeKnobAttachment(dlyKnob_, "sendDly", "Send to Echo");

        muteButton_.setButtonText("M");
        muteButton_.setTooltip("Mute channel");
        muteButton_.onClick = [this] { processor_.getEngine().channel(channel_).muted = muteButton_.getToggleState(); };
        addAndMakeVisible(muteButton_);
        soloButton_.setButtonText("S");
        soloButton_.setTooltip("Solo channel");
        soloButton_.onClick = [this] { processor_.getEngine().channel(channel_).solo = soloButton_.getToggleState(); };
        addAndMakeVisible(soloButton_);
        auxMuteButton_.setButtonText("M");
        auxMuteButton_.setTooltip("Mute effects sends (Aux M)");
        auxMuteButton_.onClick = [this] { processor_.getEngine().channel(channel_).auxMuted = auxMuteButton_.getToggleState(); };
        addAndMakeVisible(auxMuteButton_);
        auxSoloButton_.setButtonText("S");
        auxSoloButton_.setTooltip("Solo effects sends, drop the dry signal (Aux S)");
        auxSoloButton_.onClick = [this] { processor_.getEngine().channel(channel_).auxSolo = auxSoloButton_.getToggleState(); };
        addAndMakeVisible(auxSoloButton_);

        auxLabel_.setText("AUX", juce::dontSendNotification);
        auxLabel_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(auxLabel_);
        addAndMakeVisible(meter_);
    }

    void resized() override {
        int w = getWidth();
        nameLabel_.setBounds(0, 0, w, 16);
        fader_.setBounds(6, 20, 30, 120);
        meter_.setBounds(w - 12, 20, 8, 120);
        muteButton_.setBounds(2, 144, 26, 18);
        soloButton_.setBounds(w - 28, 144, 26, 18);
        int ky = 168;
        auto knobRow = [&](juce::Slider& s, int y, const char* label) {
            s.setBounds(4, y, w - 8, 30);
            juce::Label* l = knobLabels_[&s];
            if (!l) {
                l = new juce::Label();
                l->setText(label, juce::dontSendNotification);
                l->setJustificationType(juce::Justification::centred);
                l->setFont(juce::Font(9.0f));
                addAndMakeVisible(l);
                knobLabels_[&s] = l;
            }
            l->setBounds(0, y + 30, w, 11);
        };
        knobRow(panKnob_, ky, "PAN");       ky += 44;
        knobRow(toneKnob_, ky, "TONE");     ky += 44;
        knobRow(satKnob_, ky, "SAT");       ky += 44;
        auxLabel_.setBounds(0, ky, w, 11);  ky += 12;
        knobRow(revKnob_, ky, "REV");       ky += 44;
        knobRow(dlyKnob_, ky, "DLY");       ky += 44;
        auxMuteButton_.setBounds(2, ky, 26, 18);
        auxSoloButton_.setBounds(w - 28, ky, 26, 18);
        setSize(w, ky + 22);
    }

    void refresh() {
        auto& c = processor_.getEngine().channel(channel_);
        meter_.setLevel(processor_.getEngine().channelPeak(channel_));
        if (muteButton_.getToggleState() != c.muted)
            muteButton_.setToggleState(c.muted, juce::dontSendNotification);
        if (soloButton_.getToggleState() != c.solo)
            soloButton_.setToggleState(c.solo, juce::dontSendNotification);
        if (auxMuteButton_.getToggleState() != c.auxMuted)
            auxMuteButton_.setToggleState(c.auxMuted, juce::dontSendNotification);
        if (auxSoloButton_.getToggleState() != c.auxSolo)
            auxSoloButton_.setToggleState(c.auxSolo, juce::dontSendNotification);
        nameLabel_.setText(juce::String(c.kitId) + " " + juce::String(kVoices[channel_].shortName),
                           juce::dontSendNotification);
    }

private:
    SilktoneDrumMachineAudioProcessor& processor_;
    int channel_;
    juce::Label nameLabel_, auxLabel_;
    juce::Slider fader_, panKnob_, toneKnob_, satKnob_, revKnob_, dlyKnob_;
    juce::ToggleButton muteButton_, soloButton_, auxMuteButton_, auxSoloButton_;
    LevelMeter meter_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAttachment_;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> knobAttachments_;
    std::map<juce::Slider*, juce::Label*> knobLabels_;
};

class MixerTab : public RefreshableTab {
public:
    explicit MixerTab(SilktoneDrumMachineAudioProcessor& p) : processor_(p) {
        for (int ch = 0; ch < 30; ++ch) {
            auto* strip = new ChannelStrip(processor_, ch);
            strips_.add(strip);
            inner_.addAndMakeVisible(strip);
        }
        inner_.setSize(30 * 66 + 8, 430);
        viewport_.setViewedComponent(&inner_, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);
        revLabel_.setText("Reverb return", juce::dontSendNotification);
        dlyLabel_.setText("Echo return", juce::dontSendNotification);
        addAndMakeVisible(revLabel_);
        addAndMakeVisible(dlyLabel_);
        revSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        revSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 18);
        addAndMakeVisible(revSlider_);
        dlySlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        dlySlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 18);
        addAndMakeVisible(dlySlider_);
        revAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor_.getAPVTS(), "revReturn", revSlider_);
        dlyAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor_.getAPVTS(), "dlyReturn", dlySlider_);
    }

    void resized() override {
        revLabel_.setBounds(8, 4, 100, 20);
        revSlider_.setBounds(110, 4, 200, 20);
        dlyLabel_.setBounds(330, 4, 90, 20);
        dlySlider_.setBounds(420, 4, 200, 20);
        viewport_.setBounds(0, 30, getWidth(), getHeight() - 30);
        for (int ch = 0; ch < 30; ++ch)
            strips_[ch]->setBounds(4 + ch * 66, 0, 62, 430);
    }

    void refresh() override {
        for (auto* s : strips_) s->refresh();
    }

private:
    SilktoneDrumMachineAudioProcessor& processor_;
    juce::Viewport viewport_;
    juce::Component inner_;
    juce::OwnedArray<ChannelStrip> strips_;
    juce::Label revLabel_, dlyLabel_;
    juce::Slider revSlider_, dlySlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> revAttachment_, dlyAttachment_;
};

// ================================================================= SAMPLER
class SamplerTab : public RefreshableTab, private juce::ListBoxModel {
public:
    explicit SamplerTab(SilktoneDrumMachineAudioProcessor& p) : processor_(p) {
        sourceLabel_.setText(processor_.getRecordSourceDescription(), juce::dontSendNotification);
        sourceLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(sourceLabel_);

        recordButton_.setButtonText("● Record");
        recordButton_.setClickingTogglesState(true);
        recordButton_.onClick = [this] {
            if (recordButton_.getToggleState()) processor_.startRecording(voiceBox_.getSelectedId() - 1);
            else processor_.stopRecording();
        };
        addAndMakeVisible(recordButton_);

        voiceBox_.addItemList(voiceNames(), 1);
        voiceBox_.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(voiceBox_);

        chopModeBox_.addItem("Transient", 1);
        chopModeBox_.addItem("Meter / grid", 2);
        chopModeBox_.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(chopModeBox_);
        chopDivBox_.addItem("Bar", 1);
        chopDivBox_.addItem("Half bar", 2);
        chopDivBox_.addItem("Beat", 3);
        chopDivBox_.addItem("Eighth", 4);
        chopDivBox_.addItem("Sixteenth", 5);
        chopDivBox_.setSelectedId(3, juce::dontSendNotification);
        addAndMakeVisible(chopDivBox_);
        chopSensBox_.addItem("Gentle", 1);
        chopSensBox_.addItem("Balanced", 2);
        chopSensBox_.addItem("Fine", 3);
        chopSensBox_.setSelectedId(2, juce::dontSendNotification);
        addAndMakeVisible(chopSensBox_);
        tempoLockButton_.setButtonText("Tempo lock");
        tempoLockButton_.setClickingTogglesState(true);
        addAndMakeVisible(tempoLockButton_);
        chopButton_.setButtonText("Chop sample");
        chopButton_.onClick = [this] { chopSelected(); };
        addAndMakeVisible(chopButton_);
        clearSampleButton_.setButtonText("Clear sample");
        clearSampleButton_.onClick = [this] {
            processor_.clearSampleOnChannel(voiceBox_.getSelectedId() - 1);
        };
        addAndMakeVisible(clearSampleButton_);
        loadFileButton_.setButtonText("Load audio file…");
        loadFileButton_.onClick = [this] { loadFile(); };
        addAndMakeVisible(loadFileButton_);

        sliceList_.setModel(this);
        addAndMakeVisible(sliceList_);
        targetVoiceBox_.addItemList(voiceNames(), 1);
        targetVoiceBox_.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(targetVoiceBox_);
        sendSliceButton_.setButtonText("Send selected slice to pad");
        sendSliceButton_.onClick = [this] { sendSelectedSlice(); };
        addAndMakeVisible(sendSliceButton_);
        statusLabel_.setText("No sample loaded on the selected voice yet.", juce::dontSendNotification);
        addAndMakeVisible(statusLabel_);
    }

    int getNumRows() override { return (int)slices_.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
        if (row < 0 || row >= (int)slices_.size()) return;
        const auto& s = slices_[row];
        g.setColour(selected ? SilktoneLookAndFeel::amber() : SilktoneLookAndFeel::cream());
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        g.drawText("Slice " + juce::String(row + 1) + " — " +
                   juce::String(s.start, 3) + "s to " + juce::String(s.end, 3) + "s (" +
                   juce::String(s.end - s.start, 3) + "s)",
                   4, 0, w - 8, h, juce::Justification::centredLeft, true);
    }

    void resized() override {
        int w = getWidth();
        sourceLabel_.setBounds(8, 4, w - 230, 20);
        recordButton_.setBounds(8, 30, 110, 24);
        voiceBox_.setBounds(124, 30, 180, 24);
        loadFileButton_.setBounds(310, 30, 140, 24);
        clearSampleButton_.setBounds(456, 30, 120, 24);
        chopModeBox_.setBounds(8, 62, 140, 24);
        chopDivBox_.setBounds(154, 62, 120, 24);
        chopSensBox_.setBounds(280, 62, 110, 24);
        tempoLockButton_.setBounds(396, 62, 110, 24);
        chopButton_.setBounds(512, 62, 120, 24);
        sliceList_.setBounds(8, 94, w - 16, getHeight() - 160);
        targetVoiceBox_.setBounds(8, getHeight() - 58, 180, 24);
        sendSliceButton_.setBounds(194, getHeight() - 58, 220, 24);
        statusLabel_.setBounds(8, getHeight() - 28, w - 16, 20);
    }

    void refresh() override {
        int ch = voiceBox_.getSelectedId() - 1;
        auto& c = processor_.getEngine().channel(ch);
        bool recording = processor_.getEngine().isRecording();
        if (recordButton_.getToggleState() != recording)
            recordButton_.setToggleState(recording, juce::dontSendNotification);
        if (c.sample && c.sample->valid) {
            statusLabel_.setText("Sample on " + juce::String(kVoices[ch].name) + ": " +
                                 juce::String(c.sample->name) + " — " +
                                 juce::String(c.sample->left.size() / c.sample->sampleRate, 2) +
                                 "s, " + juce::String((int)c.sample->sampleRate) + " Hz" +
                                 (c.sample->tempoLock ? " (tempo locked)" : ""),
                                 juce::dontSendNotification);
            if (tempoLockButton_.getToggleState() != c.sample->tempoLock)
                tempoLockButton_.setToggleState(c.sample->tempoLock, juce::dontSendNotification);
        } else {
            statusLabel_.setText("No sample loaded on " + juce::String(kVoices[ch].name) + ".",
                                 juce::dontSendNotification);
        }
        inputLevel_ = processor_.getInputLevel();
    }

    void paint(juce::Graphics& g) override {
        // Input level meter (top right).
        auto bounds = juce::Rectangle<float>((float)getWidth() - 210.0f, 4.0f, 200.0f, 12.0f);
        g.setColour(juce::Colour(0xff1c120a));
        g.fillRoundedRectangle(bounds, 2.0f);
        g.setColour(SilktoneLookAndFeel::green());
        g.fillRoundedRectangle(bounds.getX(), bounds.getY(),
                               bounds.getWidth() * juce::jlimit(0.0f, 1.0f, inputLevel_), bounds.getHeight(), 2.0f);
        g.setColour(SilktoneLookAndFeel::creamDim());
        g.setFont(juce::Font(10.0f));
        g.drawText("INPUT", bounds.getX() - 40.0f, bounds.getY() - 2.0f, 36.0f, 16.0f,
                   juce::Justification::centredRight, false);
    }

private:
    void loadFile() {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Load an audio file onto " + juce::String(kVoices[voiceBox_.getSelectedId() - 1].name),
            juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
        fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file.existsAsFile())
                    processor_.loadSampleOntoChannel(voiceBox_.getSelectedId() - 1, file);
            });
    }

    void chopSelected() {
        int ch = voiceBox_.getSelectedId() - 1;
        bool locked = tempoLockButton_.getToggleState();
        processor_.getEngine().setSampleTempoLock(ch, locked,
                                               (int)std::lround(processor_.getEngine().bpm()));
        const char* divs[] = { "bar", "half", "beat", "eighth", "sixteenth" };
        const char* sens[] = { "gentle", "balanced", "fine" };
        slices_ = processor_.getEngine().autoChop(
            ch,
            chopModeBox_.getSelectedId() == 1 ? "transient" : "meter",
            divs[chopDivBox_.getSelectedId() - 1],
            sens[chopSensBox_.getSelectedId() - 1],
            locked);
        sliceList_.updateContent();
        sliceList_.repaint();
        statusLabel_.setText("Chopped into " + juce::String(slices_.size()) + " slice(s).",
                             juce::dontSendNotification);
    }

    void sendSelectedSlice() {
        int row = sliceList_.getSelectedRow();
        if (row < 0 || row >= (int)slices_.size()) return;
        int ch = voiceBox_.getSelectedId() - 1;
        int target = targetVoiceBox_.getSelectedId() - 1;
        processor_.getEngine().assignSliceToVoice(ch, row, target);
        statusLabel_.setText("Slice " + juce::String(row + 1) + " sent to " +
                             juce::String(kVoices[target].name) + ".", juce::dontSendNotification);
    }

    SilktoneDrumMachineAudioProcessor& processor_;
    juce::Label sourceLabel_, statusLabel_;
    juce::TextButton recordButton_, chopButton_, clearSampleButton_, loadFileButton_, sendSliceButton_;
    juce::ToggleButton tempoLockButton_{ "Tempo lock" };
    juce::ComboBox voiceBox_, chopModeBox_, chopDivBox_, chopSensBox_, targetVoiceBox_;
    juce::ListBox sliceList_;
    std::vector<Slice> slices_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    float inputLevel_ = 0.0f;
};

// ================================================================= LIBRARY
class LibraryTab : public RefreshableTab, private juce::ListBoxModel {
public:
    explicit LibraryTab(SilktoneDrumMachineAudioProcessor& p) : processor_(p) {
        scanButton_.setButtonText("Scan a folder…");
        scanButton_.onClick = [this] { scanFolder(); };
        addAndMakeVisible(scanButton_);
        searchBox_.setTextToShowWhenEmpty("Search drums by name…", SilktoneLookAndFeel::creamDim());
        searchBox_.onTextChange = [this] { applyFilter(); };
        addAndMakeVisible(searchBox_);
        favoritesOnlyButton_.setButtonText("★ Favorites only");
        favoritesOnlyButton_.setClickingTogglesState(true);
        favoritesOnlyButton_.onClick = [this] { applyFilter(); };
        addAndMakeVisible(favoritesOnlyButton_);
        voiceBox_.addItemList(voiceNames(), 1);
        voiceBox_.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(voiceBox_);
        loadButton_.setButtonText("Load selected onto voice");
        loadButton_.onClick = [this] { loadSelected(); };
        addAndMakeVisible(loadButton_);
        favoriteButton_.setButtonText("★ / ☆ Favorite");
        favoriteButton_.onClick = [this] { toggleFavorite(); };
        addAndMakeVisible(favoriteButton_);
        list_.setModel(this);
        addAndMakeVisible(list_);
        statusLabel_.setText("Scan a folder on your Mac to find drum sounds, then search and load them onto pads.",
                             juce::dontSendNotification);
        addAndMakeVisible(statusLabel_);
        applyFilter();
    }

    int getNumRows() override { return (int)filtered_.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
        if (row < 0 || row >= (int)filtered_.size()) return;
        const auto& e = processor_.getLibrary()[filtered_[row]];
        g.setColour(selected ? SilktoneLookAndFeel::amber() : SilktoneLookAndFeel::cream());
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        g.drawText((e.favorite ? "★ " : "") + e.name + "  —  " + voiceDisplayName(e.voiceId) +
                   "  (" + e.path + ")", 4, 0, w - 8, h, juce::Justification::centredLeft, true);
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override {
        if (row >= 0 && row < (int)filtered_.size()) loadSelected();
    }

    void resized() override {
        int w = getWidth();
        scanButton_.setBounds(8, 4, 130, 24);
        searchBox_.setBounds(144, 4, 300, 24);
        favoritesOnlyButton_.setBounds(450, 4, 140, 24);
        list_.setBounds(8, 34, w - 16, getHeight() - 100);
        voiceBox_.setBounds(8, getHeight() - 58, 180, 24);
        loadButton_.setBounds(194, getHeight() - 58, 200, 24);
        favoriteButton_.setBounds(400, getHeight() - 58, 140, 24);
        statusLabel_.setBounds(8, getHeight() - 28, w - 16, 20);
    }

    void refresh() override {
        statusLabel_.setText(juce::String(processor_.getLibrary().size()) + " sounds indexed, " +
                             juce::String(filtered_.size()) + " shown.",
                             juce::dontSendNotification);
    }

private:
    void scanFolder() {
        fileChooser_ = std::make_unique<juce::FileChooser>("Choose a folder of drum sounds");
        fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                auto folder = fc.getResult();
                if (folder.isDirectory()) {
                    processor_.scanLibraryFolder(folder);
                    applyFilter();
                }
            });
    }

    void applyFilter() {
        filtered_.clear();
        juce::String q = searchBox_.getText().toLowerCase();
        const auto& lib = processor_.getLibrary();
        for (int i = 0; i < (int)lib.size(); ++i) {
            if (favoritesOnlyButton_.getToggleState() && !lib[i].favorite) continue;
            if (q.isNotEmpty() && !juce::String(lib[i].name).toLowerCase().contains(q) &&
                !juce::String(voiceDisplayName(lib[i].voiceId)).toLowerCase().contains(q))
                continue;
            filtered_.push_back(i);
        }
        list_.updateContent();
        list_.repaint();
    }

    void loadSelected() {
        int row = list_.getSelectedRow();
        if (row < 0 || row >= (int)filtered_.size()) return;
        const auto& e = processor_.getLibrary()[filtered_[row]];
        processor_.loadSampleOntoChannel(voiceBox_.getSelectedId() - 1, juce::File(e.path));
        statusLabel_.setText("Loaded " + juce::String(e.name) + " onto " +
                             juce::String(kVoices[voiceBox_.getSelectedId() - 1].name) + ".",
                             juce::dontSendNotification);
    }

    void toggleFavorite() {
        int row = list_.getSelectedRow();
        if (row < 0 || row >= (int)filtered_.size()) return;
        processor_.toggleLibraryFavorite(juce::String(processor_.getLibrary()[filtered_[row]].path));
        applyFilter();
    }

    SilktoneDrumMachineAudioProcessor& processor_;
    juce::TextButton scanButton_, loadButton_, favoriteButton_;
    juce::ToggleButton favoritesOnlyButton_{ "★ Favorites only" };
    juce::TextEditor searchBox_;
    juce::ComboBox voiceBox_;
    juce::ListBox list_;
    juce::Label statusLabel_;
    std::vector<int> filtered_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
};

// =================================================================== STEMS
class StemsTab : public RefreshableTab {
public:
    explicit StemsTab(SilktoneDrumMachineAudioProcessor& p) : processor_(p) {
        noteLabel_.setText("Stem splitter: a frequency-band split of the loaded file — "
                           "low / mid / perc / high / centre / side. It is NOT AI separation.",
                           juce::dontSendNotification);
        noteLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(noteLabel_);
        loadButton_.setButtonText("Load a song or loop…");
        loadButton_.onClick = [this] { loadFile(); };
        addAndMakeVisible(loadButton_);
        sourceLabel_.setText("No file loaded.", juce::dontSendNotification);
        addAndMakeVisible(sourceLabel_);

        for (int i = 0; i < 6; ++i) {
            auto* name = new juce::Label();
            name->setText(juce::String(kStemDefs[i].name) + " — " + juce::String(kStemDefs[i].desc),
                          juce::dontSendNotification);
            addAndMakeVisible(name);
            nameLabels_.add(name);
            auto* play = new juce::TextButton("Play");
            play->onClick = [this, i] { processor_.previewStem(i); };
            addAndMakeVisible(play);
            playButtons_.add(play);
            auto* wav = new juce::TextButton("Export WAV");
            wav->onClick = [this, i] { exportStem(i); };
            addAndMakeVisible(wav);
            wavButtons_.add(wav);
            auto* voice = new juce::ComboBox();
            voice->addItemList(voiceNames(), 1);
            voice->setSelectedId(1, juce::dontSendNotification);
            addAndMakeVisible(voice);
            voiceBoxes_.add(voice);
            auto* send = new juce::TextButton("→ Pad");
            send->onClick = [this, i] {
                processor_.sendStemToPad(i, voiceBoxes_[i]->getSelectedId() - 1);
            };
            addAndMakeVisible(send);
            sendButtons_.add(send);
        }
    }

    void resized() override {
        int w = getWidth();
        noteLabel_.setBounds(8, 4, w - 16, 20);
        loadButton_.setBounds(8, 30, 180, 24);
        sourceLabel_.setBounds(194, 30, w - 200, 24);
        int y = 64;
        for (int i = 0; i < 6; ++i) {
            nameLabels_[i]->setBounds(8, y, 320, 24);
            playButtons_[i]->setBounds(336, y, 70, 24);
            wavButtons_[i]->setBounds(412, y, 100, 24);
            voiceBoxes_[i]->setBounds(518, y, 170, 24);
            sendButtons_[i]->setBounds(694, y, 70, 24);
            y += 30;
        }
    }

    void refresh() override {
        bool has = processor_.hasStemSource();
        sourceLabel_.setText(has ? "Source: " + processor_.getStemSourceName()
                                 : "No file loaded.", juce::dontSendNotification);
        for (int i = 0; i < 6; ++i) {
            playButtons_[i]->setEnabled(has);
            wavButtons_[i]->setEnabled(has);
            sendButtons_[i]->setEnabled(has);
        }
    }

private:
    void loadFile() {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Load a song or drum loop to split", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac");
        fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file.existsAsFile()) processor_.loadStemSource(file);
            });
    }

    void exportStem(int i) {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Save " + juce::String(kStemDefs[i].name) + " stem",
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                .getChildFile(juce::String(kStemDefs[i].name).toLowerCase() + "-stem.wav"),
            "*.wav");
        fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                      juce::FileBrowserComponent::canSelectFiles,
            [this, i](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file.getFullPathName().isNotEmpty())
                    processor_.exportStemWav(i, file.withFileExtension("wav"));
            });
    }

    SilktoneDrumMachineAudioProcessor& processor_;
    juce::Label noteLabel_, sourceLabel_;
    juce::TextButton loadButton_;
    juce::OwnedArray<juce::Label> nameLabels_;
    juce::OwnedArray<juce::TextButton> playButtons_, wavButtons_, sendButtons_;
    juce::OwnedArray<juce::ComboBox> voiceBoxes_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
};

// ================================================================== EXPORT
class ExportTab : public RefreshableTab {
public:
    explicit ExportTab(SilktoneDrumMachineAudioProcessor& p) : processor_(p) {
        midiButton_.setButtonText("Export MIDI (.mid)");
        midiButton_.onClick = [this] { saveFile("mid", [this](const juce::File& f) {
            processor_.exportMidi(f); }); };
        addAndMakeVisible(midiButton_);
        wavButton_.setButtonText("Export loop WAV (4 bars)");
        wavButton_.onClick = [this] { saveFile("wav", [this](const juce::File& f) {
            processor_.exportLoopWav(f, 4); }); };
        addAndMakeVisible(wavButton_);
        stemsButton_.setButtonText("Export per-voice stems (.zip)");
        stemsButton_.onClick = [this] { saveFile("zip", [this](const juce::File& f) {
            processor_.exportStemsZip(f); }); };
        addAndMakeVisible(stemsButton_);
        noteLabel_.setText("MIDI: channel 10, 96 ticks per quarter note, one note per step. "
                           "WAV loop: 4 bars plus a 0.6 s tail, no click. "
                           "Stems: one dry WAV per sounding voice, zipped.",
                           juce::dontSendNotification);
        noteLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(noteLabel_);
        statusLabel_.setText("", juce::dontSendNotification);
        addAndMakeVisible(statusLabel_);
    }

    void resized() override {
        int w = getWidth();
        midiButton_.setBounds(8, 8, 220, 28);
        wavButton_.setBounds(8, 44, 220, 28);
        stemsButton_.setBounds(8, 80, 220, 28);
        noteLabel_.setBounds(240, 8, w - 248, 100);
        statusLabel_.setBounds(8, 120, w - 16, 20);
    }

private:
    template <typename Fn>
    void saveFile(const juce::String& ext, Fn&& fn) {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Save export", juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
            "*." + ext);
        fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                      juce::FileBrowserComponent::canSelectFiles,
            [this, ext, fn = std::forward<Fn>(fn)](const juce::FileChooser& fc) mutable {
                auto file = fc.getResult().withFileExtension(ext);
                if (file.getFullPathName().isEmpty()) return;
                fn(file);
                statusLabel_.setText("Saved: " + file.getFullPathName(), juce::dontSendNotification);
            });
    }

    SilktoneDrumMachineAudioProcessor& processor_;
    juce::TextButton midiButton_, wavButton_, stemsButton_;
    juce::Label noteLabel_, statusLabel_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
};

} // namespace

// ============================================================ EDITOR ITSELF
SilktoneDrumMachineEditor::SilktoneDrumMachineEditor(SilktoneDrumMachineAudioProcessor& p)
    : AudioProcessorEditor(p), processor_(p) {
    setLookAndFeel(&lookAndFeel_);
    setSize(1100, 720);
    setResizable(true, true);
    setResizeLimits(900, 560, 1600, 1000);

    // Transport bar.
    playButton_.setClickingTogglesState(true);
    playButton_.onClick = [this] {
        if (playButton_.getToggleState()) processor_.getEngine().play();
        else processor_.getEngine().pause();
    };
    addAndMakeVisible(playButton_);
    stopButton_.onClick = [this] {
        processor_.getEngine().stop();
        playButton_.setToggleState(false, juce::dontSendNotification);
    };
    addAndMakeVisible(stopButton_);
    tapButton_.onClick = [this] { tapTempo(); };
    addAndMakeVisible(tapButton_);

    bpmSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
    bpmSlider_.setTooltip("Tempo (BPM)");
    addAndMakeVisible(bpmSlider_);
    bpmAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getAPVTS(), "bpm", bpmSlider_);

    swingSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    swingSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
    swingSlider_.setTooltip("Swing %");
    addAndMakeVisible(swingSlider_);
    swingAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getAPVTS(), "swing", swingSlider_);

    swingIntervalBox_.addItem("16th", 1);
    swingIntervalBox_.addItem("8th", 2);
    addAndMakeVisible(swingIntervalBox_);
    swingIntervalAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor_.getAPVTS(), "swingInterval", swingIntervalBox_);

    sigBox_.addItemList(sigNames(), 1);
    addAndMakeVisible(sigBox_);
    sigAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor_.getAPVTS(), "sig", sigBox_);

    kitBox_.addItemList(kitNames(), 1);
    addAndMakeVisible(kitBox_);
    kitAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor_.getAPVTS(), "kit", kitBox_);

    presetBox_.addItemList(presetNames(), 1);
    presetBox_.setTextWhenNothingSelected("Pattern…");
    presetBox_.onChange = [this] {
        int idx = presetBox_.getSelectedId() - 1;
        if (idx >= 0) processor_.loadPreset(idx);
    };
    addAndMakeVisible(presetBox_);

    masterSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    masterSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
    masterSlider_.setTooltip("Master volume");
    addAndMakeVisible(masterSlider_);
    masterAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getAPVTS(), "masterVol", masterSlider_);

    clickButton_.setClickingTogglesState(true);
    addAndMakeVisible(clickButton_);
    clickAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor_.getAPVTS(), "click", clickButton_);

    infoButton_.onClick = [this] { showInfo(); };
    addAndMakeVisible(infoButton_);

    // Tabs.
    auto* sequencer = new SequencerTab(processor_);
    auto* pads = new ScrollWrapper(new PadDeckGrid(processor_));
    auto* mixer = new MixerTab(processor_);
    auto* sampler = new SamplerTab(processor_);
    auto* library = new LibraryTab(processor_);
    auto* stems = new StemsTab(processor_);
    auto* exportTab = new ExportTab(processor_);
    tabs_.addTab("Sequencer", SilktoneLookAndFeel::woodPanel(), sequencer, true);
    tabs_.addTab("Pads", SilktoneLookAndFeel::woodPanel(), pads, true);
    tabs_.addTab("Mixer", SilktoneLookAndFeel::woodPanel(), mixer, true);
    tabs_.addTab("Sampler", SilktoneLookAndFeel::woodPanel(), sampler, true);
    tabs_.addTab("Library", SilktoneLookAndFeel::woodPanel(), library, true);
    tabs_.addTab("Stems", SilktoneLookAndFeel::woodPanel(), stems, true);
    tabs_.addTab("Export", SilktoneLookAndFeel::woodPanel(), exportTab, true);
    addAndMakeVisible(tabs_);

    startTimerHz(30);
}

SilktoneDrumMachineEditor::~SilktoneDrumMachineEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void SilktoneDrumMachineEditor::paint(juce::Graphics& g) {
    // Wood cabinet: dark brown with a subtle horizontal grain.
    g.fillAll(SilktoneLookAndFeel::woodDark());
    g.setColour(juce::Colour(0x14ffffff));
    for (int y = 8; y < getHeight(); y += 26)
        g.drawLine(0.0f, (float)y, (float)getWidth(), (float)y, 1.0f);
    // Top bar strip.
    g.setColour(SilktoneLookAndFeel::woodPanel());
    g.fillRect(0, 0, getWidth(), 48);
    g.setColour(SilktoneLookAndFeel::amber());
    g.drawLine(0.0f, 48.0f, (float)getWidth(), 48.0f, 2.0f);
}

void SilktoneDrumMachineEditor::resized() {
    int w = getWidth();
    playButton_.setBounds(8, 8, 44, 32);
    stopButton_.setBounds(56, 8, 44, 32);
    tapButton_.setBounds(104, 8, 56, 32);
    bpmSlider_.setBounds(166, 8, 130, 32);
    swingSlider_.setBounds(302, 8, 110, 32);
    swingIntervalBox_.setBounds(416, 8, 64, 32);
    sigBox_.setBounds(484, 8, 62, 32);
    kitBox_.setBounds(550, 8, 150, 32);
    presetBox_.setBounds(704, 8, 190, 32);
    masterSlider_.setBounds(898, 8, 110, 32);
    clickButton_.setBounds(1012, 8, 56, 32);
    infoButton_.setBounds(w - 40, 8, 32, 32);
    tabs_.setBounds(0, 52, w, getHeight() - 52);
}

void SilktoneDrumMachineEditor::timerCallback() {
    bool playing = processor_.getEngine().isPlaying();
    if (playButton_.getToggleState() != playing)
        playButton_.setToggleState(playing, juce::dontSendNotification);
    for (int i = 0; i < tabs_.getNumTabs(); ++i)
        if (auto* tab = dynamic_cast<RefreshableTab*>(tabs_.getTabContentComponent(i)))
            tab->refresh();
}

void SilktoneDrumMachineEditor::tapTempo() {
    double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (now - lastTapTime_ > 2.0) tapTimes_.clear();
    lastTapTime_ = now;
    tapTimes_.push_back(now);
    if (tapTimes_.size() > 5) tapTimes_.erase(tapTimes_.begin());
    if (tapTimes_.size() >= 2) {
        double sum = 0;
        for (size_t i = 1; i < tapTimes_.size(); ++i) sum += tapTimes_[i] - tapTimes_[i - 1];
        double bpm = 60.0 / (sum / (tapTimes_.size() - 1));
        bpm = juce::jlimit(40.0, 300.0, bpm);
        if (auto* p = processor_.getAPVTS().getParameter("bpm"))
            p->setValueNotifyingHost((float)((bpm - 40.0) / 260.0));
    }
}

void SilktoneDrumMachineEditor::showInfo() {
    juce::String info =
        "SILKTONE DRUM MACHINE — plugin\n\n"
        "SEQUENCER — click a step to turn it on, again for an accent, a third time to clear it. "
        "M / S mute and solo a row. ◀ ▶ scroll the sound for that row through its whole family. "
        "CLR clears the row. Copy / Paste copies one row to another.\n\n"
        "PADS — tap a pad to play it. Tap low on the pad for a soft hit, high for a hard one. "
        "Right-click a pad for mute / solo / favorite / clear sample.\n\n"
        "MIXER — one strip per voice: volume fader, PAN, TONE and SAT (saturation) knobs, "
        "and two sends, REV and DLY, routed Logic-style to the hall reverb and the tempo-synced echo. "
        "M / S mute and solo the channel; the AUX M / S pair mute and solo the sends.\n\n"
        "SAMPLER — load any audio file onto a voice, or record from the sidechain input "
        "(in Logic, pick your UAD / Apollo input in the plugin header's sidechain menu; "
        "in the Standalone build, pick your interface in the audio settings). "
        "Tempo lock plays the sample in time with the tempo. Chop splits a sample by transients "
        "or by the grid, and slices can be sent to any pad.\n\n"
        "LIBRARY — scan a folder of drum sounds on your Mac, search by name, load onto a pad, "
        "star your favorites.\n\n"
        "STEMS — drop in a full song or drum loop and it splits into six frequency bands "
        "(low / mid / perc / high / centre / side). This is a filter split, not AI separation. "
        "Play a stem, export it as WAV, or send it straight to a pad.\n\n"
        "EXPORT — MIDI (channel 10), a 4-bar WAV loop, or per-voice stems as a ZIP.\n\n"
        "TRANSPORT — ▶ play / pause, ■ stop. TAP sets the tempo from your taps. "
        "CLICK turns the metronome on (live only, never in exports). "
        "Host sync: the plugin follows Logic's tempo and transport when Sync / Follow are on "
        "(right-click the BPM to find them in the parameter list, or automate them).";
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                           "Silktone Drum Machine — info", info, "Close");
}
