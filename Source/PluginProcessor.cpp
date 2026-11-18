// PluginProcessor.cpp — see PluginProcessor.h.
#include <algorithm>
#include <cmath>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace silktone;

// ------------------------------------------------------------------ helpers
namespace {
juce::StringArray kitNames() {
    juce::StringArray names;
    for (auto id : kKitOrder) {
        for (auto& k : kKits)
            if (id == k.id) { names.add(k.name); break; }
    }
    return names;
}
juce::StringArray sigNames() {
    juce::StringArray names;
    for (auto& s : kSigs) names.add(s.name);
    return names;
}
} // namespace

// ------------------------------------------------------------- construction
SilktoneDrumMachineAudioProcessor::SilktoneDrumMachineAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         // One extra input bus: in Logic this appears as the
                         // plugin's sidechain input — route your UAD / Apollo
                         // input there to record into the sampler. In the
                         // Standalone build it is the selected audio device.
                         .withInput("Record Input", juce::AudioChannelSet::stereo(), false)),
      apvts_(*this, nullptr, "Parameters", createParameterLayout()) {
}

SilktoneDrumMachineAudioProcessor::~SilktoneDrumMachineAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout
SilktoneDrumMachineAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bpm", "Tempo", juce::NormalisableRange<float>(40.0f, 300.0f, 0.1f), 120.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "swing", "Swing", juce::NormalisableRange<float>(50.0f, 75.0f, 0.1f), 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "swingInterval", "Swing Interval", juce::StringArray{ "16th", "8th" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "sig", "Time Signature", sigNames(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "kit", "Kit", kitNames(), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "masterVol", "Master Volume", 0.0f, 1.0f, 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("click", "Click", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "revReturn", "Reverb Return", 0.0f, 1.0f, 0.15f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dlyReturn", "Echo Return", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "syncHost", "Sync to Host Tempo", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "followHost", "Follow Host Transport", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("midiOut", "MIDI Out", false));

    for (int ch = 0; ch < 30; ++ch) {
        juce::String p = "ch" + juce::String(ch) + "_";
        juce::String label = juce::String(kVoices[ch].shortName) + " ";
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "vol", label + "Volume", 0.0f, 1.0f, 0.85f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "tune", label + "Tune", juce::NormalisableRange<float>(-12.0f, 12.0f, 1.0f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "pan", label + "Pan", juce::NormalisableRange<float>(-50.0f, 50.0f, 1.0f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "tone", label + "Tone", juce::NormalisableRange<float>(-50.0f, 50.0f, 1.0f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "sat", label + "Saturation", 0.0f, 100.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "sendRev", label + "Send Reverb", 0.0f, 100.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "sendDly", label + "Send Echo", 0.0f, 100.0f, 0.0f));
    }
    return { params.begin(), params.end() };
}

// ------------------------------------------------------------------- buses
bool SilktoneDrumMachineAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    auto in = layouts.getMainInputChannelSet();
    if (in != juce::AudioChannelSet::disabled() && in != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void SilktoneDrumMachineAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    engine_.prepare(sampleRate);
    syncParametersToEngine(true);
}

void SilktoneDrumMachineAudioProcessor::releaseResources() {}

// ------------------------------------------------------- parameter -> engine
void SilktoneDrumMachineAudioProcessor::syncParametersToEngine(bool force) {
    auto getF = [&](const juce::String& id) {
        if (auto* p = apvts_.getRawParameterValue(id)) return p->load();
        return 0.0f;
    };
    auto getB = [&](const juce::String& id) { return getF(id) > 0.5f; };
    auto getI = [&](const juce::String& id) { return (int)std::lround(getF(id)); };

    float bpm = getF("bpm");
    if (force || bpm != cache_.bpm) { engine_.setBpm(bpm); cache_.bpm = bpm; }
    float swing = getF("swing");
    if (force || swing != cache_.swing) { engine_.setSwing(swing); cache_.swing = swing; }
    int swInt = getI("swingInterval");
    if (force || swInt != cache_.swingInterval) {
        engine_.setSwingInterval(swInt == 1 ? "8" : "16");
        cache_.swingInterval = swInt;
    }
    int sig = getI("sig");
    if (force || sig != cache_.sig) {
        if (sig >= 0 && sig < kNumSigs) engine_.setSig(kSigs[sig].name);
        cache_.sig = sig;
    }
    int kit = getI("kit");
    if (force || kit != cache_.kit) {
        if (kit >= 0 && kit < kNumKits) engine_.setKit(kit);
        cache_.kit = kit;
    }
    float mv = getF("masterVol");
    if (force || mv != cache_.masterVol) { engine_.setMasterVol(mv); cache_.masterVol = mv; }
    bool click = getB("click");
    if (force || !cache_.clickSet || click != cache_.click) { engine_.setClick(click); cache_.click = click; cache_.clickSet = true; }
    float rev = getF("revReturn");
    if (force || rev != cache_.revReturn) { engine_.setReverbReturn(rev); cache_.revReturn = rev; }
    float dly = getF("dlyReturn");
    if (force || dly != cache_.dlyReturn) { engine_.setDelayReturn(dly); cache_.dlyReturn = dly; }
    bool syncH = getB("syncHost");
    if (force || !cache_.syncHostSet || syncH != cache_.syncHost) {
        engine_.setSyncToHost(syncH); cache_.syncHost = syncH; cache_.syncHostSet = true;
    }
    bool follow = getB("followHost");
    if (force || !cache_.followHostSet || follow != cache_.followHost) { cache_.followHost = follow; cache_.followHostSet = true; }
    bool midiOut = getB("midiOut");
    if (force || !cache_.midiOutSet || midiOut != cache_.midiOut) {
        engine_.setMidiOut(midiOut); cache_.midiOut = midiOut; cache_.midiOutSet = true;
    }
    for (int ch = 0; ch < 30; ++ch) {
        juce::String p = "ch" + juce::String(ch) + "_";
        auto& c = engine_.channel(ch);
        float v = getF(p + "vol");
        if (force || v != cache_.vol[ch]) { c.vol = v; cache_.vol[ch] = v; }
        v = getF(p + "tune");
        if (force || v != cache_.tune[ch]) { c.tuneSemitones = v; cache_.tune[ch] = v; }
        v = getF(p + "pan");
        if (force || v != cache_.pan[ch]) { c.pan = v; cache_.pan[ch] = v; }
        v = getF(p + "tone");
        if (force || v != cache_.tone[ch]) { c.tone = v; cache_.tone[ch] = v; }
        v = getF(p + "sat");
        if (force || v != cache_.sat[ch]) { c.saturation = v; cache_.sat[ch] = v; }
        v = getF(p + "sendRev");
        if (force || v != cache_.sendRev[ch]) { c.sendRev = v; cache_.sendRev[ch] = v; }
        v = getF(p + "sendDly");
        if (force || v != cache_.sendDly[ch]) { c.sendDly = v; cache_.sendDly[ch] = v; }
    }
}

// ------------------------------------------------------------------ process
void SilktoneDrumMachineAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                    juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    syncParametersToEngine(false);

    // Host transport + tempo.
    if (auto* playhead = getPlayHead()) {
        if (auto pos = playhead->getPosition()) {
            if (pos->getBpm()) engine_.setHostBpm(*pos->getBpm());
            bool hostPlaying = pos->getIsPlaying();
            if (cache_.followHost) {
                if (hostPlaying && !hostWasPlaying_ && !engine_.isPlaying())
                    engine_.play();
                else if (!hostPlaying && hostWasPlaying_ && engine_.isPlaying())
                    engine_.pause();
            }
            hostWasPlaying_ = hostPlaying;
        }
    }

    // MIDI input: notes trigger pads (GM drum notes, as in the app).
    for (const auto metadata : midiMessages) {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            int note = msg.getNoteNumber();
            for (int ch = 0; ch < 30; ++ch) {
                int midi = 36;
                for (auto& vd : kVoices)
                    if (engine_.channel(ch).instId == vd.id) { midi = vd.midiNote; break; }
                if (midi == note) {
                    engine_.triggerChannel(ch, msg.getVelocity() / 127.0);
                    break;
                }
            }
        }
    }
    midiMessages.clear();

    // Sidechain / record-input bus.
    const float* sideL = nullptr;
    const float* sideR = nullptr;
    if (totalNumInputChannels >= 2) {
        auto inBus = getBusBuffer(buffer, true, 0);
        sideL = inBus.getReadPointer(0);
        sideR = inBus.getReadPointer(1);
        float peak = 0;
        for (int i = 0; i < buffer.getNumSamples(); i += 8)
            peak = std::max(peak, std::fabs(sideL[i]) + std::fabs(sideR[i]));
        inputLevel_.store(peak);
    } else {
        inputLevel_.store(0.0f);
    }

    auto outBus = getBusBuffer(buffer, false, 0);
    const int numOutChannels = outBus.getNumChannels();
    if (numOutChannels >= 2) {
        engine_.processBlock(outBus.getWritePointer(0), outBus.getWritePointer(1),
                             buffer.getNumSamples(), sideL, sideR);
    } else if (numOutChannels == 1) {
        // Mono output bus (auval's 1-channel test, a mono Logic track):
        // the engine renders stereo, so render to scratch and fold the
        // left channel out.
        const int n = buffer.getNumSamples();
        monoOutL_.assign((size_t)n, 0.0f);
        monoOutR_.assign((size_t)n, 0.0f);
        engine_.processBlock(monoOutL_.data(), monoOutR_.data(), n, sideL, sideR);
        float* dst = outBus.getWritePointer(0);
        for (int i = 0; i < n; ++i) dst[i] = monoOutL_[(size_t)i];
    }

    // Stem / sample preview player, mixed over the output.
    if (previewing_.load() && previewBuffer_.getNumSamples() > 0) {
        int pos = previewPos_.load();
        int n = buffer.getNumSamples();
        float* oL = outBus.getWritePointer(0);
        float* oR = numOutChannels >= 2 ? outBus.getWritePointer(1) : nullptr;
        for (int i = 0; i < n && pos < previewBuffer_.getNumSamples(); ++i, ++pos) {
            oL[i] += previewBuffer_.getSample(0, pos) * 0.8f;
            if (oR != nullptr)
                oR[i] += previewBuffer_.getSample(previewBuffer_.getNumChannels() > 1 ? 1 : 0, pos) * 0.8f;
        }
        previewPos_.store(pos);
        if (pos >= previewBuffer_.getNumSamples()) previewing_.store(false);
    }

    // MIDI out: the pattern as notes on channel 10.
    if (engine_.midiOutEnabled()) {
        for (auto& e : engine_.takeMidiOutEvents()) {
            if (e.noteOn)
                midiMessages.addEvent(juce::MidiMessage::noteOn(10, e.note, (juce::uint8)e.velocity),
                                      e.sampleOffset);
            else
                midiMessages.addEvent(juce::MidiMessage::noteOff(10, e.note), e.sampleOffset);
        }
    } else {
        engine_.takeMidiOutEvents();  // drain
    }
}

juce::AudioProcessorEditor* SilktoneDrumMachineAudioProcessor::createEditor() {
    return new SilktoneDrumMachineEditor(*this);
}

// ------------------------------------------------------------- UI actions
void SilktoneDrumMachineAudioProcessor::loadPreset(int presetIndex) {
    engine_.loadPreset(presetIndex);
    patternIndex_ = presetIndex;
    // Keep the host parameters in step with the loaded preset.
    if (auto* p = apvts_.getParameter("bpm"))
        p->setValueNotifyingHost((float)((engine_.bpm() - 40) / 260.0));
    if (auto* p = apvts_.getParameter("swing"))
        p->setValueNotifyingHost((float)((engine_.swing() - 50) / 25.0));
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts_.getParameter("sig"))) {
        for (int i = 0; i < kNumSigs; ++i)
            if (engine_.sig() == kSigs[i].name) { p->setValueNotifyingHost((float)i / (kNumSigs - 1)); break; }
    }
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts_.getParameter("kit"))) {
        p->setValueNotifyingHost((float)engine_.kitIndex() / (kNumKits - 1));
    }
}

void SilktoneDrumMachineAudioProcessor::exportMidi(const juce::File& file) {
    auto bytes = engine_.buildMidi();
    file.replaceWithData(bytes.data(), (int)bytes.size());
}

void SilktoneDrumMachineAudioProcessor::exportLoopWav(const juce::File& file, int bars) {
    std::vector<float> L, R;
    engine_.renderLoop(bars, L, R);
    auto wav = encodeWavStereo(L.data(), R.data(), (int)L.size(), (int)engine_.sampleRate());
    file.replaceWithData(wav.data(), (int)wav.size());
}

void SilktoneDrumMachineAudioProcessor::exportStemsZip(const juce::File& file) {
    std::vector<DrumEngine::StemAudio> stems;
    std::vector<std::string> names;
    engine_.renderStems(stems, names);
    juce::ZipFile::Builder builder;
    for (size_t i = 0; i < stems.size(); ++i) {
        auto wav = encodeWavStereo(stems[i].l.data(), stems[i].r.data(),
                                   (int)stems[i].l.size(), (int)engine_.sampleRate());
        juce::String name = juce::String(names[i]).replaceCharacter(' ', '-').toLowerCase();
        builder.addEntry(new juce::MemoryInputStream(wav.data(), wav.size(), true),
                         9, name + "-stem.wav", juce::Time::getCurrentTime());
    }
    juce::FileOutputStream out(file);
    if (out.openedOk()) builder.writeToStream(out, nullptr);
}

void SilktoneDrumMachineAudioProcessor::loadSampleOntoChannel(int channel, const juce::File& file) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (!reader) return;
    juce::AudioBuffer<float> buf((int)reader->numChannels, (int)reader->lengthInSamples);
    reader->read(&buf, 0, (int)reader->lengthInSamples, 0, true, true);
    loadSampleOntoChannel(channel, file.getFileNameWithoutExtension(), buf, reader->sampleRate);
}

void SilktoneDrumMachineAudioProcessor::loadSampleOntoChannel(int channel, const juce::String& name,
                                                             const juce::AudioBuffer<float>& buffer,
                                                             double sampleRate) {
    if (channel < 0 || channel >= 30) return;
    auto sm = std::make_shared<UserSample>();
    sm->sampleRate = sampleRate;
    sm->name = name.toStdString();
    sm->chopBpm = (int)std::lround(engine_.bpm());
    sm->tempoLock = false;
    sm->valid = true;
    int n = buffer.getNumSamples();
    sm->left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + n);
    if (buffer.getNumChannels() > 1)
        sm->right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + n);
    engine_.setSample(channel, sm);
}

void SilktoneDrumMachineAudioProcessor::clearSampleOnChannel(int channel) {
    engine_.clearSample(channel);
}

void SilktoneDrumMachineAudioProcessor::scanLibraryFolder(const juce::File& folder) {
    library_ = engine_.scanFolder(folder.getFullPathName().toStdString());
}

void SilktoneDrumMachineAudioProcessor::toggleLibraryFavorite(const juce::String& path) {
    for (auto& e : library_)
        if (e.path == path.toStdString()) { e.favorite = !e.favorite; break; }
}

bool SilktoneDrumMachineAudioProcessor::loadStemSource(const juce::File& file) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (!reader) return false;
    juce::AudioBuffer<float> buf(2, (int)reader->lengthInSamples);
    reader->read(&buf, 0, (int)reader->lengthInSamples, 0, true, true);
    if (reader->numChannels < 2)
        buf.copyFrom(1, 0, buf.getReadPointer(0), buf.getNumSamples());  // mono -> dual mono
    auto split = DrumEngine::splitStems(buf.getReadPointer(0), buf.getReadPointer(1),
                                        buf.getNumSamples(), reader->sampleRate, 2);
    // kStemDefs order is low, mid, perc, high, center, side — same as the struct.
    splitStems_ = { split.low, split.mid, split.perc, split.high, split.center, split.side };
    stemSourceName_ = file.getFileName();
    stemSourceSampleRate_ = reader->sampleRate;
    return true;
}

void SilktoneDrumMachineAudioProcessor::sendStemToPad(int stemIndex, int voiceIndex) {
    if (stemIndex < 0 || stemIndex >= (int)splitStems_.size() || voiceIndex < 0 || voiceIndex >= 30)
        return;
    const auto& st = splitStems_[stemIndex];
    auto sm = std::make_shared<UserSample>();
    sm->sampleRate = stemSourceSampleRate_;
    sm->name = stemSourceName_.toStdString() + " — " + kStemDefs[stemIndex].name;
    sm->chopBpm = (int)std::lround(engine_.bpm());
    sm->valid = true;
    sm->left = st.l;
    sm->right = st.r;
    engine_.setSample(voiceIndex, sm);
}

void SilktoneDrumMachineAudioProcessor::exportStemWav(int stemIndex, const juce::File& file) {
    if (stemIndex < 0 || stemIndex >= (int)splitStems_.size()) return;
    const auto& st = splitStems_[stemIndex];
    auto wav = encodeWavStereo(st.l.data(), st.r.data(), (int)st.l.size(), (int)stemSourceSampleRate_);
    file.replaceWithData(wav.data(), (int)wav.size());
}

void SilktoneDrumMachineAudioProcessor::startRecording(int targetChannel) {
    recordTargetChannel_ = targetChannel;
    engine_.startRecording();
}

std::shared_ptr<UserSample> SilktoneDrumMachineAudioProcessor::stopRecording() {
    auto sm = engine_.stopRecording("Recorded take");
    if (sm) engine_.setSample(recordTargetChannel_, sm);
    return sm;
}

juce::String SilktoneDrumMachineAudioProcessor::getRecordSourceDescription() const {
    if (wrapperType == wrapperType_AudioUnit || wrapperType == wrapperType_VST3)
        return "Sidechain input — in Logic, choose your UAD / Apollo input in the "
               "plugin header's sidechain menu.";
    return "Audio device input (Standalone) — pick your interface in the audio settings.";
}

void SilktoneDrumMachineAudioProcessor::startPreview(const juce::AudioBuffer<float>& buffer) {
    previewBuffer_ = buffer;
    previewPos_.store(0);
    previewing_.store(true);
}
void SilktoneDrumMachineAudioProcessor::stopPreview() { previewing_.store(false); }

bool SilktoneDrumMachineAudioProcessor::hasStemSource() const { return !splitStems_.empty(); }
juce::String SilktoneDrumMachineAudioProcessor::stemSourceName() const { return stemSourceName_; }

void SilktoneDrumMachineAudioProcessor::previewStem(int stemIndex) {
    if (stemIndex < 0 || stemIndex >= (int)splitStems_.size()) return;
    const auto& st = splitStems_[stemIndex];
    juce::AudioBuffer<float> buf(2, (int)st.l.size());
    buf.copyFrom(0, 0, st.l.data(), (int)st.l.size());
    buf.copyFrom(1, 0, st.r.data(), (int)st.r.size());
    startPreview(buf);
}

// -------------------------------------------------------------------- state
juce::String SilktoneDrumMachineAudioProcessor::stepsToString(int channel) const {
    const auto& c = engine_.channel(channel);
    juce::String s;
    for (int i = 0; i < engine_.steps(); ++i)
        s += c.steps[i] == 2 ? 'X' : (c.steps[i] == 1 ? 'x' : '.');
    return s;
}

void SilktoneDrumMachineAudioProcessor::stepsFromString(int channel, const juce::String& s) {
    auto& c = engine_.channel(channel);
    auto parsed = DrumEngine::parsePat(s.toStdString(), engine_.steps());
    for (int i = 0; i < engine_.steps(); ++i) {
        c.steps[i] = parsed[i];
        c.vel[i] = parsed[i] == 2 ? 1.0f : 0.72f;
    }
}

void SilktoneDrumMachineAudioProcessor::writeSampleToWavBytes(const UserSample& sample,
                                                             juce::MemoryBlock& out) const {
    std::vector<uint8_t> wav;
    if (sample.right.empty())
        wav = encodeWavMono(sample.left.data(), (int)sample.left.size(), (int)sample.sampleRate);
    else
        wav = encodeWavStereo(sample.left.data(), sample.right.data(),
                              (int)sample.left.size(), (int)sample.sampleRate);
    out.setSize(wav.size());
    if (!wav.empty()) std::memcpy(out.getData(), wav.data(), wav.size());
}

void SilktoneDrumMachineAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    juce::ValueTree state("SilktoneDrumMachine");
    state.setProperty("patternIndex", patternIndex_, nullptr);
    state.setProperty("version", 1, nullptr);

    juce::ValueTree chans("Channels");
    for (int ch = 0; ch < 30; ++ch) {
        const auto& c = engine_.channel(ch);
        juce::ValueTree node("Ch");
        node.setProperty("steps", stepsToString(ch), nullptr);
        juce::String vel;
        for (int i = 0; i < engine_.steps(); ++i) {
            if (i) vel += ",";
            vel += juce::String(c.vel[i], 2);
        }
        node.setProperty("vel", vel, nullptr);
        node.setProperty("muted", c.muted, nullptr);
        node.setProperty("solo", c.solo, nullptr);
        node.setProperty("auxMuted", c.auxMuted, nullptr);
        node.setProperty("auxSolo", c.auxSolo, nullptr);
        node.setProperty("favorite", c.favorite, nullptr);
        node.setProperty("kitId", juce::String(c.kitId), nullptr);
        node.setProperty("instId", juce::String(c.instId), nullptr);
        if (c.sample && c.sample->valid) {
            juce::MemoryBlock wav;
            writeSampleToWavBytes(*c.sample, wav);
            node.setProperty("sampleWav", wav, nullptr);
            node.setProperty("sampleName", juce::String(c.sample->name), nullptr);
            node.setProperty("sampleTempoLock", c.sample->tempoLock, nullptr);
            node.setProperty("sampleChopBpm", c.sample->chopBpm, nullptr);
        }
        chans.appendChild(node, nullptr);
    }
    state.appendChild(chans, nullptr);
    state.appendChild(apvts_.copyState(), nullptr);

    juce::MemoryOutputStream mos(destData, false);
    state.writeToStream(mos);
}

void SilktoneDrumMachineAudioProcessor::applyChannelStateFromValueTree(const juce::ValueTree& state) {
    auto chans = state.getChildWithName("Channels");
    if (!chans.isValid()) return;
    for (int ch = 0; ch < 30 && ch < chans.getNumChildren(); ++ch) {
        auto node = chans.getChild(ch);
        auto& c = engine_.channel(ch);
        stepsFromString(ch, node.getProperty("steps", "").toString());
        auto velStr = node.getProperty("vel", "").toString();
        if (velStr.isNotEmpty()) {
            juce::StringArray parts;
            parts.addTokens(velStr, ",", "");
            for (int i = 0; i < engine_.steps() && i < parts.size(); ++i)
                c.vel[i] = (float)parts[i].getDoubleValue();
        }
        c.muted = node.getProperty("muted", false);
        c.solo = node.getProperty("solo", false);
        c.auxMuted = node.getProperty("auxMuted", false);
        c.auxSolo = node.getProperty("auxSolo", false);
        c.favorite = node.getProperty("favorite", false);
        c.kitId = node.getProperty("kitId", "").toString().toStdString();
        juce::String inst = node.getProperty("instId", "").toString();
        if (inst.isNotEmpty()) c.instId = inst.toStdString();
        if (node.hasProperty("sampleWav")) {
            juce::MemoryBlock wav = *node.getProperty("sampleWav").getBinaryData();
            juce::String name = node.getProperty("sampleName", "Loaded sample").toString();
            if (engine_.loadSampleWav(ch, (const uint8_t*)wav.getData(), wav.getSize(),
                                      name.toStdString())) {
                engine_.setSampleTempoLock(ch, node.getProperty("sampleTempoLock", false),
                                           node.getProperty("sampleChopBpm", 120));
            }
        }
    }
}

void SilktoneDrumMachineAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    juce::ValueTree state;
    {
        juce::MemoryInputStream mis(data, (size_t)sizeInBytes, false);
        state = juce::ValueTree::readFromStream(mis);
    }
    if (!state.isValid()) return;
    patternIndex_ = state.getProperty("patternIndex", -1);

    auto apvtsState = state.getChildWithName("Parameters");
    if (apvtsState.isValid())
        apvts_.replaceState(apvtsState);
    syncParametersToEngine(true);  // apply sig/kit/bpm etc. first
    applyChannelStateFromValueTree(state);  // then the saved steps override
}

//==============================================================================
// This creates new instances of the plugin — the entry point the JUCE
// VST3/AU/Standalone wrappers link against.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SilktoneDrumMachineAudioProcessor();
}
