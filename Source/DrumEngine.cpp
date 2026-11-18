// DrumEngine.cpp — see DrumEngine.h. Faithful port of the sequencer, mixer,
// sampler and export behaviour in drum-machine.html.
#include "DrumEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace silktone {

namespace {
int sigStepsFor(const std::string& s) {
    for (auto& d : kSigs) if (s == d.name) return d.steps;
    return 16;
}
const SigDef* sigDefFor(const std::string& s) {
    for (auto& d : kSigs) if (s == d.name) return &d;
    return &kSigs[0];
}
const KitDef* kitDefFor(const std::string& id) {
    for (auto& k : kKits) if (id == k.id) return &k;
    return &kKits[0];
}
int kitIndexFor(const std::string& id) {
    for (int i = 0; i < kNumKits; ++i) if (id == kKitOrder[i]) return i;
    return 0;
}
const PresetDef* presetFor(int idx) {
    if (idx < 0 || idx >= kNumPresets) return nullptr;
    return &kPresets[idx];
}
double tuneRatio(float semitones) { return std::pow(2.0, semitones / 12.0); }
} // namespace

// ------------------------------------------------------------------ prepare
DrumEngine::DrumEngine() {
    for (int i = 0; i < kNumChannels; ++i) {
        auto& ch = channels_[i];
        ch.voiceIndex = i;
        ch.instId = kVoices[i].id;
        ch.vel.fill(0.72f);
    }
    // Startup pattern, like the web app's loadPreset('house') on open.
    int idx = 0;
    for (int i = 0; i < kNumPresets; ++i)
        if (std::string(kPresets[i].key) == "house") { idx = i; break; }
    loadPreset(idx);
}

void DrumEngine::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    noise_.init(sampleRate);
    hall_.prepare(sampleRate, 1.8);
    echo_.prepare(sampleRate);
    echo_.setDelaySeconds(stepDurationSeconds() * 3.0);
    comp_.prepare(sampleRate);
    renderClick(false, sampleRate, clickHit_[0]);
    renderClick(true, sampleRate, clickHit_[1]);
    revBus_.clear(); dlyBus_.clear();
    for (auto& s : slots_) s.active = false;
    // NOTE: channel state (steps, volumes, mute/solo, samples, scrolled
    // sounds) is deliberately NOT reset here. prepare() runs on every
    // prepareToPlay, which a host can call after restoring saved state —
    // wiping channels there would destroy the session's pattern.
    samplePos_ = 0; nextStepSample_ = 0; curStep_ = 0;
    playing_ = paused_ = false;
    pending_.clear();
}

void DrumEngine::reset() {
    for (auto& s : slots_) s.active = false;
    hall_.reset(); echo_.reset(); comp_.reset();
    pending_.clear();
    samplePos_ = 0; nextStepSample_ = 0; curStep_ = 0;
    playing_ = paused_ = false;
}

// ------------------------------------------------------------ transport
void DrumEngine::play() {
    playing_ = true; paused_ = false;
    curStep_ = 0;
    nextStepSample_ = samplePos_ + (int64_t)(0.06 * sampleRate_);
    echo_.setDelaySeconds(stepDurationSeconds() * 3.0);
    pending_.clear();
}
void DrumEngine::resume() {
    if (playing_) return;
    playing_ = true; paused_ = false;
    nextStepSample_ = samplePos_ + (int64_t)(0.06 * sampleRate_);
    echo_.setDelaySeconds(stepDurationSeconds() * 3.0);
    pending_.clear();
}
void DrumEngine::pause() {
    if (!playing_) return;
    playing_ = false; paused_ = true;
    pending_.clear();
}
void DrumEngine::stop() {
    playing_ = false; paused_ = false; curStep_ = 0;
    pending_.clear();
    for (auto& s : slots_) s.active = false;
}
void DrumEngine::togglePlay() {
    if (playing_) pause();
    else if (paused_) resume();
    else play();
}
void DrumEngine::setHostBpm(double bpm) { if (syncToHost_ && bpm > 20 && bpm < 400) setBpm(bpm); }

void DrumEngine::setBpm(double bpm) {
    bpm_ = clampd(bpm, 40.0, 300.0);
    echo_.setDelaySeconds(stepDurationSeconds() * 3.0);
}
void DrumEngine::setSwing(double pct) { swing_ = clampd(pct, 50.0, 75.0); }
void DrumEngine::setSwingInterval(const std::string& iv) { swingInterval_ = (iv == "8") ? "8" : "16"; }
int DrumEngine::steps() const { return sigStepsFor(sig_); }
std::string DrumEngine::kitId() const { return kKitOrder[kitIndex_]; }

void DrumEngine::setSig(const std::string& s) {
    bool known = false;
    for (auto& d : kSigs) if (s == d.name) { known = true; break; }
    if (!known || s == sig_) return;
    std::string from = sig_;
    sig_ = s;
    // Re-fit every row, beat by beat (remapBar, as in the app).
    for (auto& ch : channels_) {
        std::vector<int> oldSteps(ch.steps.begin(), ch.steps.begin() + sigStepsFor(from));
        std::vector<float> oldVel(ch.vel.begin(), ch.vel.begin() + sigStepsFor(from));
        std::vector<float> newVel;
        std::vector<int> ns = remapBar(oldSteps, oldVel, from, s, newVel);
        ch.steps.fill(0); ch.vel.fill(0.72f);
        for (size_t i = 0; i < ns.size() && i < ch.steps.size(); ++i) {
            ch.steps[i] = ns[i];
            ch.vel[i] = i < newVel.size() ? newVel[i] : (ns[i] == 2 ? 1.0f : 0.72f);
        }
    }
    curStep_ = 0;
}

void DrumEngine::setKit(int kitIndex) {
    if (kitIndex < 0 || kitIndex >= kNumKits) return;
    kitIndex_ = kitIndex;
    for (auto& ch : channels_) ch.kitId.clear();  // re-home every channel
}

void DrumEngine::setMasterVol(double v) { masterVol_ = clampd(v, 0.0, 1.0); }
void DrumEngine::setClick(bool on) { clickOn_ = on; }
void DrumEngine::setReverbReturn(double v) { reverbReturn_ = clampd(v, 0.0, 1.0); }
void DrumEngine::setDelayReturn(double v) { delayReturn_ = clampd(v, 0.0, 1.0); }
void DrumEngine::setSyncToHost(bool on) { syncToHost_ = on; }

// ------------------------------------------------------------- pattern
std::vector<int> DrumEngine::parsePat(const std::string& str, int len) {
    std::vector<int> a(len, 0);
    if (str.empty()) return a;
    for (int i = 0; i < len; ++i) {
        char c = str[(size_t)i % str.size()];
        a[i] = (c == 'X') ? 2 : (c == 'x' ? 1 : 0);
    }
    return a;
}

std::vector<int> DrumEngine::remapBar(const std::vector<int>& steps, const std::vector<float>& vel,
                                      const std::string& fromSig, const std::string& toSig,
                                      std::vector<float>& outVel) {
    const SigDef* F = sigDefFor(fromSig);
    const SigDef* T = sigDefFor(toSig);
    int n = T->steps;
    std::vector<int> ns(n, 0);
    outVel.assign(n, 0.72f);
    auto beatTable = [](const SigDef* S) {
        std::vector<std::pair<int,int>> t;
        for (int i = 0; i < S->numGroups; ++i) {
            int st = S->groups[i];
            int en = (i + 1 < S->numGroups) ? S->groups[i + 1] : S->steps;
            t.push_back({ st, en - st });
        }
        return t;
    };
    auto ft = beatTable(F), tt = beatTable(T);
    for (size_t i = 0; i < steps.size(); ++i) {
        int v = steps[i];
        if (!v) continue;
        int bi = 0;
        for (size_t k = 0; k < ft.size(); ++k) if ((int)i >= ft[k].first) bi = (int)k;
        if (bi >= (int)tt.size()) continue;
        int noff = (int)std::lround(((double)((int)i - ft[bi].first) / ft[bi].second) * tt[bi].second);
        if (noff >= tt[bi].second) noff = tt[bi].second - 1;
        int j = tt[bi].first + noff;
        if (v > ns[j]) {
            ns[j] = v;
            outVel[j] = (i < vel.size()) ? vel[i] : (v == 2 ? 1.0f : 0.72f);
        }
    }
    return ns;
}

void DrumEngine::loadPreset(int presetIndex) {
    const PresetDef* p = presetFor(presetIndex);
    if (!p) return;
    if (p->sig[0] && p->sig != sig_) setSig(p->sig);
    if (p->kit[0]) setKit(kitIndexFor(p->kit));
    int n = steps();
    for (int ci = 0; ci < kNumChannels; ++ci) {
        auto& ch = channels_[ci];
        const char* str = p->rows[ci];
        if (p->layer && !str) continue;  // percussion loops layer over the drums
        std::string s = str ? str : "";
        std::vector<int> parsed = parsePat(s, (int)s.size() ? (int)s.size() : n);
        std::vector<float> pvel(parsed.size());
        for (size_t i = 0; i < parsed.size(); ++i) pvel[i] = parsed[i] == 2 ? 1.0f : 0.72f;
        if ((int)parsed.size() == n) {
            for (int i = 0; i < n; ++i) { ch.steps[i] = parsed[i]; ch.vel[i] = pvel[i]; }
        } else if ((int)s.size() == 16) {
            std::vector<float> nv;
            std::vector<int> rr = remapBar(parsed, pvel, "4/4", sig_, nv);
            for (int i = 0; i < n; ++i) { ch.steps[i] = rr[i]; ch.vel[i] = nv[i]; }
        } else {
            std::vector<int> pp = parsePat(s, n);
            for (int i = 0; i < n; ++i) { ch.steps[i] = pp[i]; ch.vel[i] = pp[i] == 2 ? 1.0f : 0.72f; }
        }
        for (int i = n; i < kMaxSteps; ++i) { ch.steps[i] = 0; ch.vel[i] = 0.72f; }
    }
    if (p->bpm) setBpm(p->bpm);
    if (p->swing) setSwing(p->swing);
    if (p->swingInt[0]) setSwingInterval(p->swingInt);
}

void DrumEngine::loadPercLoop(int loopIndex) {
    if (loopIndex < 0 || loopIndex >= kNumPercLoops) return;
    // Percussion loops are presets perc1..perc20 with layer:true.
    for (int i = 0; i < kNumPresets; ++i)
        if (std::string(kPresets[i].key) == kPercLoops[loopIndex].key) { loadPreset(i); return; }
}

void DrumEngine::clearRow(int ch) {
    if (ch < 0 || ch >= kNumChannels) return;
    channels_[ch].steps.fill(0);
    channels_[ch].vel.fill(0.72f);
}
void DrumEngine::clearAll() { for (int i = 0; i < kNumChannels; ++i) clearRow(i); }

void DrumEngine::copyRow(int ch) {
    if (ch < 0 || ch >= kNumChannels) return;
    clipboardSteps_ = channels_[ch].steps;
    clipboardVel_ = channels_[ch].vel;
    clipboardValid_ = true;
}
void DrumEngine::pasteRow(int ch) {
    if (!clipboardValid_ || ch < 0 || ch >= kNumChannels) return;
    channels_[ch].steps = clipboardSteps_;
    channels_[ch].vel = clipboardVel_;
}
void DrumEngine::setStep(int ch, int step, int value) {
    if (ch < 0 || ch >= kNumChannels || step < 0 || step >= kMaxSteps) return;
    channels_[ch].steps[step] = value;
    if (value > 0 && channels_[ch].vel[step] <= 0) channels_[ch].vel[step] = value == 2 ? 1.0f : 0.72f;
}
void DrumEngine::setStepVelocity(int ch, int step, float v) {
    if (ch < 0 || ch >= kNumChannels || step < 0 || step >= kMaxSteps) return;
    channels_[ch].vel[step] = clampf(v, 0.0f, 1.0f);
}
float DrumEngine::stepVelocity(int ch, int step) const {
    if (ch < 0 || ch >= kNumChannels || step < 0 || step >= kMaxSteps) return 0.72f;
    return channels_[ch].vel[step];
}
void DrumEngine::applySwingMachine(int machineIndex) {
    if (machineIndex < 0 || machineIndex >= kNumSwingMachines) return;
    setSwing(kSwingMachines[machineIndex].percent);
}

// ------------------------------------------------------------ channels
bool DrumEngine::anyChannelSolo() const {
    for (auto& ch : channels_) if (ch.solo) return true;
    return false;
}
bool DrumEngine::anyAuxSolo() const {
    for (auto& ch : channels_) if (ch.auxSolo) return true;
    return false;
}
bool DrumEngine::isChannelAudible(int ch) const {
    if (ch < 0 || ch >= kNumChannels) return false;
    const auto& c = channels_[ch];
    return !c.muted && (!anyChannelSolo() || c.solo);
}

KitParams DrumEngine::kitParamsFor(const std::string& kitId) const {
    const KitDef* k = kitDefFor(kitId);
    KitParams p;
    p.kickF = k->kickF; p.kickD = k->kickD; p.snareT = k->snareT; p.hatD = k->hatD;
    p.bright = k->bright; p.real = k->real; p.muffle = k->muffle; p.ring = k->ring;
    p.snap = k->snap; p.dust = k->dust; p.grit = k->grit; p.lofi = k->lofi;
    p.branch = k->id;
    return p;
}

std::string DrumEngine::channelSoundName(int ch) const {
    if (ch < 0 || ch >= kNumChannels) return "";
    const auto& c = channels_[ch];
    std::string kid = c.kitId.empty() ? kitId() : c.kitId;
    const KitDef* k = kitDefFor(kid);
    const VoiceDef* v = nullptr;
    for (auto& vd : kVoices) if (c.instId == vd.id) { v = &vd; break; }
    return std::string(k ? k->name : kid) + " " + (v ? v->name : c.instId);
}

void DrumEngine::setChannelKit(int ch, const std::string& kitId) {
    if (ch < 0 || ch >= kNumChannels) return;
    channels_[ch].kitId = kitId;
}

void DrumEngine::scrollChannelSound(int ch, int dir) {
    if (ch < 0 || ch >= kNumChannels) return;
    auto& c = channels_[ch];
    // Family is fixed by the channel's home voice.
    const FamilyDef* fam = nullptr;
    for (auto& f : kFamilies) {
        for (int i = 0; i < f.numMembers; ++i)
            // NOTE: compare as strings, not pointers — the "kick" literal in
            // kVoices and the one in kFamilies are different objects, and
            // pointer equality only held when the compiler pooled literals
            // (it did at -O2, not at -O0). That is why the scroll test failed
            // on an unoptimised build.
            if (std::string(kVoices[c.voiceIndex].id) == f.members[i]) { fam = &f; break; }
        if (fam) break;
    }
    if (!fam) return;
    // Library: instrument-first, then kits (KIt_ORDER) — as in the app.
    std::vector<std::pair<std::string,std::string>> lib;
    for (int m = 0; m < fam->numMembers; ++m)
        for (int k = 0; k < kNumKits; ++k)
            lib.push_back({ kKitOrder[k], fam->members[m] });
    std::string curKit = c.kitId.empty() ? kitId() : c.kitId;
    int idx = 0;
    for (size_t i = 0; i < lib.size(); ++i)
        if (lib[i].first == curKit && lib[i].second == c.instId) { idx = (int)i; break; }
    int n = (int)lib.size();
    idx = ((idx + dir) % n + n) % n;
    c.kitId = lib[idx].first;
    c.instId = lib[idx].second;
}

double DrumEngine::swingDelaySeconds(int step) const {
    if (swing_ <= 50) return 0;
    bool off = false;
    int unit = 1;
    if (swingInterval_ == "8") { off = (step % 4 == 2); unit = 2; }
    else { off = (step % 2 == 1); unit = 1; }
    if (!off) return 0;
    return (swing_ - 50) / 50.0 * unit * stepDurationSeconds();
}

bool DrumEngine::isGroupStart(int step) const {
    const SigDef* s = sigDefFor(sig_);
    for (int i = 0; i < s->numGroups; ++i) if (s->groups[i] == step) return true;
    return false;
}

// ------------------------------------------------------------------ audio
void DrumEngine::triggerChannel(int ch, double velocity) {
    if (ch < 0 || ch >= kNumChannels) return;
    if (!isChannelAudible(ch)) return;
    triggerHit(ch, velocity, samplePos_, false, false);
}

void DrumEngine::triggerHit(int ch, double velocity, int64_t atSample, bool click, bool accent) {
    // Triggers that fall past the current block are deferred to the next one.
    if (atSample >= blockEnd_) {
        pending_.push_back({ atSample, ch, velocity, click, accent });
        return;
    }
    createSlot(ch, velocity, atSample, click, accent);
}

void DrumEngine::triggerClick(int64_t atSample, bool accent) {
    triggerHit(-1, 1.0, atSample, true, accent);
}

void DrumEngine::createSlot(int ch, double velocity, int64_t atSample, bool click, bool accent) {
    Slot* slot = nullptr;
    for (auto& s : slots_) if (!s.active) { slot = &s; break; }
    if (!slot) return;  // pool exhausted — drop the hit (48 concurrent max)
    slot->active = true;
    slot->channel = ch;
    slot->isClick = click;
    slot->position = 0;
    slot->rate = 1.0;
    slot->isSample = false;
    slot->sample.reset();
    slot->startOffset = (int)std::max<int64_t>(0, atSample - samplePos_);

    if (click) {
        slot->hit = accent ? clickHit_[1] : clickHit_[0];
        slot->panL = slot->panR = 1.0f;
        slot->sendRev = slot->sendDly = 0;
        slot->vol = 1.0f;
        return;
    }

    auto& c = channels_[ch];
    slot->vol = (float)(velocity * c.vol);

    // Pan: equal-power, pan -50..50 -> -1..1.
    double panNorm = clampd(c.pan / 50.0, -1.0, 1.0);
    double ang = (panNorm + 1.0) * M_PI / 4.0;
    slot->panL = (float)std::cos(ang);
    slot->panR = (float)std::sin(ang);

    // Sends, Logic-style aux routing with M/S.
    double rev = c.sendRev / 100.0, dly = c.sendDly / 100.0;
    if (c.auxMuted) { rev = 0; dly = 0; }
    else if (anyAuxSolo() && !c.auxSolo) { rev = 0; dly = 0; }
    slot->sendRev = (float)rev;
    slot->sendDly = (float)dly;
    if (c.auxSolo && !rendering_) slot->vol = 0;  // aux-solo drops the dry signal live

    if (c.sample && c.sample->valid) {
        slot->isSample = true;
        slot->sample = c.sample;
        double stretch = 1.0;
        if (c.sample->tempoLock && c.sample->chopBpm > 0)
            stretch = bpm_ / (double)c.sample->chopBpm;
        slot->rate = tuneRatio(c.tuneSemitones) * stretch * (c.sample->sampleRate / sampleRate_);
        return;
    }

    std::string kid = c.kitId.empty() ? kitId() : c.kitId;
    KitParams kp = kitParamsFor(kid);
    renderVoiceHit(c.instId.c_str(), kp, velocity, tuneRatio(c.tuneSemitones),
                   sampleRate_, noise_, slot->hit);
    // Saturation at trigger time, exactly as the app's per-hit waveshaper.
    double sat = clampd(c.saturation / 100.0, 0.0, 1.0);
    if (sat > 0) {
        for (int i = 0; i < slot->hit.numSamples && i < (int)slot->hit.samples.size(); ++i)
            slot->hit.samples[i] = (float)applySat(slot->hit.samples[i], sat);
    }
    // TONE: a tilt EQ around 800 Hz. (The app's TONE knob exists in the UI
    // but is never read by its audio path — verified by inspection — so the
    // plugin implements the labelled behaviour instead of a dead knob.)
    if (c.tone != 0) {
        double t = clampd(c.tone / 50.0, -1.0, 1.0);
        double a = 1.0 - std::exp(-2.0 * M_PI * 800.0 / sampleRate_);
        double lp = 0;
        for (int i = 0; i < slot->hit.numSamples && i < (int)slot->hit.samples.size(); ++i) {
            double x = slot->hit.samples[i];
            lp += a * (x - lp);
            double hp = x - lp;
            slot->hit.samples[i] = (float)(x + t * 0.5 * (hp - lp));
        }
    }
}

void DrumEngine::scheduleStep(int step) {
    int64_t stepSample = nextStepSample_;
    int64_t swingSamples = (int64_t)(swingDelaySeconds(step) * sampleRate_);
    if (clickOn_ && isGroupStart(step))
        triggerClick(stepSample, step == 0);  // click is never swung
    for (int ch = 0; ch < kNumChannels; ++ch) {
        if (!isChannelAudible(ch)) continue;
        int v = channels_[ch].steps[step];
        if (v <= 0) continue;
        triggerHit(ch, stepVelocity(ch, step), stepSample + swingSamples, false, false);
        if (midiOutEnabled_) {
            int note = 36;
            for (auto& vd : kVoices) if (channels_[ch].instId == vd.id) { note = vd.midiNote; break; }
            int vel127 = std::max(1, std::min(127, (int)std::lround(stepVelocity(ch, step) * 127)));
            int64_t onAt = stepSample + swingSamples;
            if (onAt < blockEnd_)
                midiOutEvents_.push_back({ (int)(onAt - samplePos_), note, vel127, true });
            int64_t offAt = onAt + (int64_t)(stepDurationSeconds() * 0.9 * sampleRate_);
            if (offAt < blockEnd_)
                midiOutEvents_.push_back({ (int)(offAt - samplePos_), note, 0, false });
            else
                midiOffPending_.push_back({ offAt, note });
        }
    }
}

std::vector<DrumEngine::MidiOutEvent> DrumEngine::takeMidiOutEvents() {
    std::vector<MidiOutEvent> out;
    out.swap(midiOutEvents_);
    return out;
}

void DrumEngine::processBlock(float* outL, float* outR, int numSamples,
                              const float* sideL, const float* sideR) {
    int64_t blockStart = samplePos_;
    int64_t blockEnd = blockStart + numSamples;
    blockEnd_ = blockEnd;

    // Recording from the sidechain / input bus.
    if (recording_ && sideL && sideR) {
        recordL_.insert(recordL_.end(), sideL, sideL + numSamples);
        recordR_.insert(recordR_.end(), sideR, sideR + numSamples);
    }

    // Fire pending triggers whose time falls in this block.
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->atSample < blockEnd) {
            createSlot(it->channel, it->velocity, it->atSample, it->click, it->accent);
            it = pending_.erase(it);
        } else ++it;
    }

    // Fire pending MIDI note-offs whose time falls in this block.
    for (auto it = midiOffPending_.begin(); it != midiOffPending_.end();) {
        if (it->atSample < blockEnd) {
            midiOutEvents_.push_back({ (int)(it->atSample - blockStart), it->note, 0, false });
            it = midiOffPending_.erase(it);
        } else ++it;
    }

    // Schedule steps whose boundary falls in this block.
    if (playing_) {
        while (nextStepSample_ < blockEnd) {
            scheduleStep(curStep_);
            nextStepSample_ += (int64_t)(stepDurationSeconds() * sampleRate_);
            curStep_ = (curStep_ + 1) % steps();
        }
    }

    // Mix.
    if ((int)revBus_.size() < numSamples) { revBus_.assign(numSamples, 0.0f); dlyBus_.assign(numSamples, 0.0f); }
    std::fill(outL, outL + numSamples, 0.0f);
    std::fill(outR, outR + numSamples, 0.0f);
    std::fill(revBus_.begin(), revBus_.begin() + numSamples, 0.0f);
    std::fill(dlyBus_.begin(), dlyBus_.begin() + numSamples, 0.0f);

    for (auto& s : slots_) {
        if (!s.active) continue;
        int start = s.startOffset;
        if (start >= numSamples) continue;
        float pk = 0;
        if (s.isSample && s.sample) {
            const auto& sm = *s.sample;
            int nCh = sm.right.empty() ? 1 : 2;
            double len = (double)sm.left.size();
            for (int i = start; i < numSamples; ++i) {
                double pos = s.position;
                if (pos >= len - 1) { s.active = false; break; }
                int ip = (int)pos;
                double frac = pos - ip;
                float l = (float)(sm.left[ip] * (1 - frac) + sm.left[ip + 1] * frac);
                float r = nCh == 2 ? (float)(sm.right[ip] * (1 - frac) + sm.right[ip + 1] * frac) : l;
                float g = s.vol;
                float ml = l * g * s.panL, mr = r * g * s.panR;
                outL[i] += ml; outR[i] += mr;
                revBus_[i] += l * g * s.sendRev;
                dlyBus_[i] += l * g * s.sendDly;
                pk = std::max(pk, std::fabs(ml) + std::fabs(mr));
                s.position += s.rate;
            }
        } else {
            const auto& hb = s.hit;
            int len = std::min(hb.numSamples, (int)hb.samples.size());
            for (int i = start; i < numSamples; ++i) {
                int ip = (int)s.position;
                if (ip >= len) { s.active = false; break; }
                float m = hb.samples[ip] * s.vol;
                outL[i] += m * s.panL;
                outR[i] += m * s.panR;
                revBus_[i] += m * s.sendRev;
                dlyBus_[i] += m * s.sendDly;
                pk = std::max(pk, std::fabs(m));
                s.position += 1.0;
            }
        }
        s.startOffset = 0;  // after the first block the slot plays from its position
        if (!s.isClick && s.channel >= 0)
            channelPeaks_[s.channel] = std::max(channelPeaks_[s.channel], pk);
    }

    // Effects returns.
    if (reverbReturn_ > 0) {
        hall_.process(revBus_.data(), revBus_.data(), numSamples);  // mono hall, both channels
        for (int i = 0; i < numSamples; ++i) {
            outL[i] += revBus_[i] * (float)reverbReturn_;
            outR[i] += revBus_[i] * (float)reverbReturn_;
        }
    }
    if (delayReturn_ > 0) {
        echo_.process(dlyBus_.data(), dlyBus_.data(), numSamples);
        float g = (float)(delayReturn_ * 0.7);  // app: dGain = (value/100) * 0.7
        for (int i = 0; i < numSamples; ++i) {
            outL[i] += dlyBus_[i] * g;
            outR[i] += dlyBus_[i] * g;
        }
    }

    // Master compressor + master level.
    comp_.process(outL, outR, numSamples);
    float mg = (float)masterVol_;
    for (int i = 0; i < numSamples; ++i) { outL[i] *= mg; outR[i] *= mg; }

    // Decay the channel peak meters.
    for (int ch = 0; ch < kNumChannels; ++ch) channelPeaks_[ch] *= 0.92f;

    samplePos_ = blockEnd;
}

// ------------------------------------------------------------- sampler
bool DrumEngine::loadSampleWav(int ch, const uint8_t* bytes, size_t size, const std::string& name) {
    if (ch < 0 || ch >= kNumChannels || !bytes || size < 44) return false;
    auto u32 = [&](size_t o) -> uint32_t {
        return (uint32_t)bytes[o] | ((uint32_t)bytes[o+1] << 8) | ((uint32_t)bytes[o+2] << 16) | ((uint32_t)bytes[o+3] << 24);
    };
    auto u16 = [&](size_t o) -> uint16_t { return (uint16_t)(bytes[o] | (bytes[o+1] << 8)); };
    if (std::memcmp(bytes, "RIFF", 4) != 0 || std::memcmp(bytes + 8, "WAVE", 4) != 0) return false;
    size_t pos = 12;
    int audioFormat = 0, numChannels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0, dataPos = 0, dataLen = 0;
    while (pos + 8 <= size) {
        uint32_t chunkSize = u32(pos + 4);
        if (std::memcmp(bytes + pos, "fmt ", 4) == 0) {
            audioFormat = u16(pos + 8);
            numChannels = u16(pos + 10);
            sampleRate = u32(pos + 12);
            bitsPerSample = u16(pos + 22);
        } else if (std::memcmp(bytes + pos, "data", 4) == 0) {
            dataPos = (uint32_t)(pos + 8);
            dataLen = chunkSize;
            break;
        }
        pos += 8 + chunkSize + (chunkSize & 1);
    }
    if (!dataPos || !numChannels || !sampleRate || !bitsPerSample) return false;
    int bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample == 0) return false;
    size_t frames = std::min<size_t>(dataLen / (bytesPerSample * numChannels),
                                     (size - dataPos) / (bytesPerSample * numChannels));
    auto sm = std::make_shared<UserSample>();
    sm->sampleRate = sampleRate;
    sm->name = name;
    sm->valid = true;
    sm->left.resize(frames);
    if (numChannels > 1) sm->right.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < numChannels && c < 2; ++c) {
            size_t o = dataPos + (i * numChannels + c) * bytesPerSample;
            float v = 0;
            if (audioFormat == 3 && bitsPerSample == 32) {
                float f; std::memcpy(&f, bytes + o, 4); v = f;
            } else if (bitsPerSample == 16) {
                int16_t s = (int16_t)u16(o); v = s / 32768.0f;
            } else if (bitsPerSample == 24) {
                int32_t s = (int32_t)(bytes[o] | (bytes[o+1] << 8) | (bytes[o+2] << 16));
                if (s & 0x800000) s |= ~0xFFFFFF;
                v = s / 8388608.0f;
            } else if (bitsPerSample == 32) {
                int32_t s = (int32_t)u32(o); v = s / 2147483648.0f;
            }
            if (c == 0) sm->left[i] = v; else sm->right[i] = v;
        }
    }
    channels_[ch].sample = sm;
    channels_[ch].slices.clear();
    return true;
}

void DrumEngine::setSample(int ch, std::shared_ptr<UserSample> sample) {
    if (ch < 0 || ch >= kNumChannels) return;
    channels_[ch].sample = sample;
    channels_[ch].slices.clear();
}
void DrumEngine::clearSample(int ch) {
    if (ch < 0 || ch >= kNumChannels) return;
    channels_[ch].sample.reset();
    channels_[ch].slices.clear();
}
void DrumEngine::setSampleTempoLock(int ch, bool on, int chopBpm) {
    if (ch < 0 || ch >= kNumChannels || !channels_[ch].sample) return;
    channels_[ch].sample->tempoLock = on;
    channels_[ch].sample->chopBpm = chopBpm;
}

// Transient detection — port of detectSlicesForBuffer() in the app.
std::vector<Slice> DrumEngine::detectSlices(const float* data, int numSamples, int numChannels,
                                            double sampleRate, const std::string& sensitivity) {
    struct Sens { double factor, minGap, minDur, splitAbove; };
    Sens sens{ 0.15, 0.16, 0.09, 1.0 };  // balanced
    if (sensitivity == "gentle") sens = { 0.22, 0.30, 0.16, 1.4 };
    else if (sensitivity == "fine") sens = { 0.09, 0.09, 0.05, 0.7 };
    double dur = numSamples / sampleRate;
    int frame = std::max(64, (int)(sampleRate * 0.005));
    int nF = std::max(1, numSamples / frame);
    std::vector<double> env(nF, 0);
    double maxEnv = 0;
    for (int f = 0; f < nF; ++f) {
        double mx = 0;
        int st = f * frame;
        for (int i = 0; i < frame; i += 4) {
            double s = 0;
            for (int c = 0; c < numChannels; ++c) s += data[(st + i) * numChannels + c];
            s = std::fabs(s / numChannels);
            if (s > mx) mx = s;
        }
        env[f] = mx;
        if (mx > maxEnv) maxEnv = mx;
    }
    if (maxEnv < 0.02) return { { 0, dur } };
    double thresh = std::max(0.03, maxEnv * sens.factor);
    struct Seg { int s, e; };
    std::vector<Seg> segs;
    bool inSeg = false;
    Seg cur{ 0, 0 };
    for (int f = 0; f < nF; ++f) {
        if (env[f] > thresh) { if (!inSeg) { cur = { f, f }; inSeg = true; } else cur.e = f; }
        else if (inSeg) { segs.push_back(cur); inSeg = false; }
    }
    if (inSeg) segs.push_back(cur);
    std::vector<Seg> merged;
    for (auto& sg : segs) {
        if (!merged.empty() && (sg.s - merged.back().e) * 0.005 < sens.minGap) merged.back().e = sg.e;
        else merged.push_back(sg);
    }
    int gapF = (int)(sens.minGap / 0.005);
    std::vector<Seg> split;
    for (auto& sg : merged) {
        if ((sg.e - sg.s) * 0.005 <= sens.splitAbove) { split.push_back(sg); continue; }
        std::vector<int> cuts{ sg.s };
        int lastCut = sg.s;
        for (int f2 = sg.s + gapF; f2 < sg.e - gapF; ++f2) {
            double prev = 0; int cnt = 0;
            for (int k = std::max(sg.s, f2 - 6); k < f2; ++k) { prev += env[k]; ++cnt; }
            prev = cnt ? prev / cnt : 0;
            if (env[f2] > thresh * 1.5 && env[f2] > prev * 1.9 + 0.03 && (f2 - lastCut) * 0.005 >= sens.minGap) {
                cuts.push_back(f2); lastCut = f2;
            }
        }
        cuts.push_back(sg.e);
        for (size_t ci = 0; ci + 1 < cuts.size(); ++ci) split.push_back({ cuts[ci], cuts[ci + 1] });
    }
    std::vector<Slice> out;
    for (auto& sg : split) {
        if ((sg.e - sg.s) * 0.005 < sens.minDur) continue;
        double start = std::max(0.0, sg.s * 0.005 - 0.02);
        double end = std::min(dur, (sg.e + 1) * 0.005 + 0.05);
        if (!out.empty() && start < out.back().end) start = out.back().end;
        if (end - start >= 0.04) out.push_back({ start, end });
    }
    if (out.empty()) out.push_back({ 0, dur });
    return out;
}

std::vector<Slice> DrumEngine::gridSlices(double duration, double bpm, const std::string& sig,
                                          const std::string& division) {
    double barBeats = sigStepsFor(sig) / 4.0;
    double beats = 1.0;
    if (division == "bar") beats = barBeats;
    else if (division == "half") beats = barBeats / 2;
    else if (division == "eighth") beats = 0.5;
    else if (division == "sixteenth") beats = 0.25;
    double g = beats * 60.0 / bpm;
    if (!(g > 0.03)) return { { 0, duration } };
    std::vector<Slice> out;
    double t = 0;
    while (t < duration - 0.03 && out.size() < 200) {
        double e = std::min(duration, t + g);
        if (e - t >= 0.04) out.push_back({ t, e });
        t += g;
    }
    if (out.empty()) out.push_back({ 0, duration });
    return out;
}

std::vector<Slice> DrumEngine::quantizeSlices(std::vector<Slice> slices, double bpm,
                                              const std::string& sig, const std::string& division,
                                              double duration) {
    double barBeats = sigStepsFor(sig) / 4.0;
    double beats = 1.0;
    if (division == "bar") beats = barBeats;
    else if (division == "half") beats = barBeats / 2;
    else if (division == "eighth") beats = 0.5;
    else if (division == "sixteenth") beats = 0.25;
    double g = beats * 60.0 / bpm;
    if (!(g > 0)) return slices;
    std::vector<Slice> out;
    for (auto& sl : slices) {
        double s = std::round(sl.start / g) * g, e = std::round(sl.end / g) * g;
        if (e <= s) e = s + g;
        s = std::max(0.0, std::min(s, duration - 0.03));
        e = std::max(s + 0.04, std::min(e, duration));
        if (!out.empty() && s < out.back().end) s = out.back().end;
        if (e - s >= 0.04) out.push_back({ s, e });
    }
    return out.empty() ? slices : out;
}

std::vector<Slice> DrumEngine::autoChop(int ch, const std::string& mode, const std::string& division,
                                        const std::string& sensitivity, bool locked) {
    if (ch < 0 || ch >= kNumChannels) return {};
    auto& sm = channels_[ch].sample;
    if (!sm || !sm->valid) return {};
    double dur = sm->left.size() / sm->sampleRate;
    std::vector<Slice> raw;
    if (mode == "meter") {
        raw = gridSlices(dur, bpm_, sig_, division);
    } else {
        // Interleave for the detector.
        std::vector<float> inter;
        int nCh = sm->right.empty() ? 1 : 2;
        inter.reserve(sm->left.size() * nCh);
        for (size_t i = 0; i < sm->left.size(); ++i) {
            inter.push_back(sm->left[i]);
            if (nCh == 2) inter.push_back(sm->right[i]);
        }
        raw = detectSlices(inter.data(), (int)sm->left.size(), nCh, sm->sampleRate, sensitivity);
        if (locked) raw = quantizeSlices(raw, bpm_, sig_, division, dur);
    }
    channels_[ch].slices = raw;
    return raw;
}

void DrumEngine::assignSliceToVoice(int ch, int sliceIndex, int voiceIndex) {
    if (ch < 0 || ch >= kNumChannels || voiceIndex < 0 || voiceIndex >= kNumChannels) return;
    auto& sm = channels_[ch].sample;
    if (!sm || !sm->valid) return;
    if (sliceIndex < 0 || sliceIndex >= (int)channels_[ch].slices.size()) return;
    const auto& sl = channels_[ch].slices[sliceIndex];
    auto out = std::make_shared<UserSample>();
    out->sampleRate = sm->sampleRate;
    out->name = sm->name + " — slice " + std::to_string(sliceIndex + 1);
    out->tempoLock = sm->tempoLock;
    out->chopBpm = (int)std::lround(bpm_);
    out->valid = true;
    int s0 = (int)(sl.start * sm->sampleRate);
    int s1 = std::min((int)sm->left.size(), (int)std::ceil(sl.end * sm->sampleRate));
    if (s1 <= s0) return;
    out->left.assign(sm->left.begin() + s0, sm->left.begin() + s1);
    if (!sm->right.empty()) out->right.assign(sm->right.begin() + s0, sm->right.begin() + s1);
    channels_[voiceIndex].sample = out;
    channels_[voiceIndex].slices.clear();
}

// ----------------------------------------------------- library scan
std::string DrumEngine::categorizeFilename(const std::string& filename) {
    std::string n = " " + filename;
    for (auto& c : n) c = (char)std::tolower((unsigned char)c);
    // strip extension
    size_t dot = n.rfind('.');
    if (dot != std::string::npos) n = n.substr(0, dot);
    for (auto& c : n) if (c == '_' || c == '.' || c == '-') c = ' ';
    n += " ";
    auto has = [&](const std::string& s) { return n.find(s) != std::string::npos; };
    if (has(" darbuka tek ") || has(" doumbek tek ") || has(" tek ")) return "dartek";
    if (has(" darbuka ") || has(" doumbek ") || has(" dumbek ") || has(" goblet ")) return "darbuka";
    if (has(" tombak bak ") || has(" tonbak bak ") || has(" bak slap ") || has(" zarb bak ")) return "tombakbak";
    if (has(" tombak ") || has(" tonbak ") || has(" zarb ")) return "tombak";
    if (has(" dayereh ") || has(" daireh ") || has(" doyra ")) return "dayereh";
    if (has(" daf ") || has(" duff ") || has(" def ")) return "daf";
    if (has(" dohol ") || has(" duhul ")) return "dohol";
    if (has(" riq ") || has(" rik ") || has(" katem ")) return "riq";
    if (has(" crash ") || has(" cymbal ") || has(" ride ")) return "crash";
    if (has(" brush ")) return "brush";
    if (has(" gated ")) return "gated";
    if (has(" sub kick ") || has(" subkick ") || has(" sub bass ") || has(" 808 kick ") || has(" sub ")) return "sub";
    if (has(" timbale ") || has(" timbal ")) return "timbale";
    if (has(" djembe ") || has(" jembe ")) return "djembe";
    if (has(" agogo ")) return "agogo";
    if (has(" crackle ") || has(" vinyl ")) return "crackle";
    if (has(" clave ")) return "clave";
    if (has(" cowbell ") || has(" cow ")) return "cow";
    if (has(" tambourine ") || has(" tamb ") || has(" tambourin ")) return "tamb";
    if (has(" bongo ")) return "bongo";
    if (has(" conga ") || has(" quinto ") || has(" tumba ")) return "conga";
    if (has(" shaker ") || has(" maraca ") || has(" cabasa ") || has(" shake ")) return "shaker";
    if (has(" rimshot ") || has(" rim ") || has(" sidestick ") || has(" cross stick ")) return "rim";
    if (has(" open hat ") || has(" openhat ") || has(" open hh ") || has(" ohat ") || has(" open ")) return "ohat";
    if (has(" floor tom ") || has(" low tom ") || has(" lotom ") || has(" ltom ")) return "ltom";
    if (has(" hi tom ") || has(" high tom ") || has(" rack tom ") || has(" hitom ") || has(" htom ") || has(" tom ")) return "htom";
    if (has(" closed hat ") || has(" closedhat ") || has(" hihat ") || has(" hi hat ") || has(" hat ") || has(" hh ") || has(" chat ")) return "chat";
    if (has(" clap ") || has(" handclap ")) return "clap";
    if (has(" snare ") || has(" sd ")) return "snare";
    if (has(" kick ") || has(" bass drum ") || has(" bassdrum ") || has(" bd ")) return "kick";
    return "";
}

std::vector<LibraryEntry> DrumEngine::scanFolder(const std::string& path) {
    std::vector<LibraryEntry> out;
    std::error_code ec;
    if (!fs::exists(path, ec)) return out;
    static const char* exts[] = { ".wav", ".aif", ".aiff", ".mp3", ".flac", ".ogg", ".m4a" };
    for (auto it = fs::recursive_directory_iterator(path, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        bool ok = false;
        for (auto e : exts) if (ext == e) { ok = true; break; }
        if (!ok) continue;
        LibraryEntry en;
        en.path = it->path().string();
        en.name = it->path().stem().string();
        en.voiceId = categorizeFilename(it->path().filename().string());
        out.push_back(en);
    }
    std::sort(out.begin(), out.end(), [](const LibraryEntry& a, const LibraryEntry& b) { return a.name < b.name; });
    return out;
}

std::vector<LibraryEntry> DrumEngine::searchLibrary(const std::vector<LibraryEntry>& lib,
                                                    const std::string& query) {
    std::string q = query;
    for (auto& c : q) c = (char)std::tolower((unsigned char)c);
    std::vector<LibraryEntry> out;
    for (auto& e : lib) {
        std::string n = e.name, v = e.voiceId;
        for (auto& c : n) c = (char)std::tolower((unsigned char)c);
        if (n.find(q) != std::string::npos || v.find(q) != std::string::npos) out.push_back(e);
    }
    return out;
}

// ------------------------------------------------------- stem splitter
DrumEngine::SplitStems DrumEngine::splitStems(const float* l, const float* r, int numSamples,
                                              double sampleRate, int stages) {
    SplitStems out;
    auto runChain = [&](const std::vector<std::pair<Biquad::Type,double>>& chain,
                        const float* inL, const float* inR,
                        std::vector<float>& oL, std::vector<float>& oR) {
        oL.assign(inL, inL + numSamples);
        oR.assign(inR, inR + numSamples);
        for (auto& [type, freq] : chain) {
            Biquad fl, fr;
            fl.set(type, freq, 0.7, sampleRate);
            fr.set(type, freq, 0.7, sampleRate);
            for (int i = 0; i < numSamples; ++i) {
                oL[i] = (float)fl.process(oL[i]);
                oR[i] = (float)fr.process(oR[i]);
            }
        }
    };
    std::vector<std::pair<Biquad::Type,double>> lowChain, midChain, percChain, highChain;
    for (int i = 0; i < stages; ++i) lowChain.push_back({ Biquad::LowPass, 150 });
    midChain.push_back({ Biquad::HighPass, 180 });
    for (int i = 0; i < std::max(1, stages - 1); ++i) midChain.push_back({ Biquad::LowPass, 1000 });
    percChain.push_back({ Biquad::HighPass, 1000 });
    percChain.push_back({ Biquad::LowPass, 6500 });
    if (stages > 2) percChain.push_back({ Biquad::LowPass, 6500 });
    for (int i = 0; i < stages; ++i) highChain.push_back({ Biquad::HighPass, 6500 });

    runChain(lowChain, l, r, out.low.l, out.low.r);
    runChain(midChain, l, r, out.mid.l, out.mid.r);
    runChain(percChain, l, r, out.perc.l, out.perc.r);
    runChain(highChain, l, r, out.high.l, out.high.r);

    // Centre: (L+R)/2 on both channels, then HP 120 / LP 8000 (Q 1, app default).
    out.center.l.resize(numSamples); out.center.r.resize(numSamples);
    {
        std::vector<float> mid(numSamples);
        for (int i = 0; i < numSamples; ++i) mid[i] = (l[i] + r[i]) * 0.5f;
        Biquad hp, lp;
        hp.set(Biquad::HighPass, 120, 1.0, sampleRate);
        lp.set(Biquad::LowPass, 8000, 1.0, sampleRate);
        for (int i = 0; i < numSamples; ++i) {
            float v = (float)lp.process(hp.process(mid[i]));
            out.center.l[i] = v; out.center.r[i] = v;
        }
    }
    // Sides: L-R and R-L.
    out.side.l.resize(numSamples); out.side.r.resize(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        out.side.l[i] = l[i] - r[i];
        out.side.r[i] = r[i] - l[i];
    }
    return out;
}

// -------------------------------------------------------------- export
std::vector<uint8_t> DrumEngine::buildMidi() const {
    const int TPQ = 96, stepTicks = TPQ / 4;
    struct Ev { int t, note, vel; };
    std::vector<Ev> events;
    int n = steps();
    for (int ch = 0; ch < kNumChannels; ++ch) {
        if (!isChannelAudible(ch)) continue;
        int note = 36;
        for (auto& vd : kVoices) if (channels_[ch].instId == vd.id) { note = vd.midiNote; break; }
        for (int s = 0; s < n; ++s) {
            int v = channels_[ch].steps[s];
            if (v > 0) {
                int vel = (int)std::lround(stepVelocity(ch, s) * 127);
                events.push_back({ s * stepTicks, note, std::max(1, std::min(127, vel)) });
            }
        }
    }
    std::sort(events.begin(), events.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    auto vlq = [](int v, std::vector<uint8_t>& out) {
        uint8_t b[4]; int n = 0;
        b[n++] = (uint8_t)(v & 127); v >>= 7;
        while (v > 0) { b[n++] = (uint8_t)((v & 127) | 128); v >>= 7; }
        for (int i = n - 1; i >= 0; --i) out.push_back(b[i]);
    };
    std::vector<uint8_t> track;
    // Time-signature meta, so Logic opens the file in the right meter.
    int num = 4, den = 2;
    {
        size_t slash = sig_.find('/');
        if (slash != std::string::npos) {
            num = std::stoi(sig_.substr(0, slash));
            den = (sig_.substr(slash + 1) == "8") ? 3 : 2;
        }
    }
    track.push_back(0); track.push_back(0xFF); track.push_back(0x58); track.push_back(4);
    track.push_back((uint8_t)num); track.push_back((uint8_t)den); track.push_back(24); track.push_back(8);
    int tempo = (int)std::lround(60000000.0 / bpm_);
    track.push_back(0); track.push_back(0xFF); track.push_back(0x51); track.push_back(3);
    track.push_back((uint8_t)((tempo >> 16) & 255)); track.push_back((uint8_t)((tempo >> 8) & 255));
    track.push_back((uint8_t)(tempo & 255));
    int last = 0;
    int offDelta = (int)(stepTicks * 0.9);  // app: vlq(stepTicks*.9) -> 21 (truncated)
    int offAdvance = (int)std::lround(stepTicks * 0.9);  // app: last += round(21.6) = 22
    for (auto& e : events) {
        vlq(e.t - last, track);
        track.push_back(0x99); track.push_back((uint8_t)e.note); track.push_back((uint8_t)e.vel);
        vlq(offDelta, track);
        track.push_back(0x89); track.push_back((uint8_t)e.note); track.push_back(0);
        last = e.t + offAdvance;
    }
    track.push_back(0); track.push_back(0xFF); track.push_back(0x2F); track.push_back(0);

    std::vector<uint8_t> file;
    auto push32 = [&](uint32_t v) {
        file.push_back((uint8_t)((v >> 24) & 255)); file.push_back((uint8_t)((v >> 16) & 255));
        file.push_back((uint8_t)((v >> 8) & 255)); file.push_back((uint8_t)(v & 255));
    };
    file.insert(file.end(), { 'M', 'T', 'h', 'd' });
    push32(6);
    file.push_back(0); file.push_back(0); file.push_back(0); file.push_back(1);
    file.push_back(0); file.push_back((uint8_t)TPQ);
    file.insert(file.end(), { 'M', 'T', 'r', 'k' });
    push32((uint32_t)track.size());
    file.insert(file.end(), track.begin(), track.end());
    return file;
}

void DrumEngine::renderLoop(int bars, std::vector<float>& outL, std::vector<float>& outR) {
    // Save live state.
    bool wasPlaying = playing_, wasPaused = paused_;
    int savedStep = curStep_;
    double savedMaster = masterVol_, savedRev = reverbReturn_, savedDly = delayReturn_;
    bool savedClick = clickOn_;

    rendering_ = true;
    masterVol_ = 0.85; reverbReturn_ = 0; delayReturn_ = 0; clickOn_ = false;
    for (auto& s : slots_) s.active = false;
    hall_.reset(); echo_.reset(); comp_.reset();
    samplePos_ = 0; nextStepSample_ = 0; curStep_ = 0;
    playing_ = true; paused_ = false;
    nextStepSample_ = (int64_t)(0.05 * sampleRate_);

    double barSec = steps() * stepDurationSeconds();
    int64_t total = (int64_t)((bars * barSec + 0.6) * sampleRate_);
    outL.assign(total, 0.0f);
    outR.assign(total, 0.0f);
    const int block = 4096;
    std::vector<float> bL(block), bR(block);
    int64_t done = 0;
    while (done < total) {
        int n = (int)std::min<int64_t>(block, total - done);
        processBlock(bL.data(), bR.data(), n, nullptr, nullptr);
        std::copy(bL.begin(), bL.begin() + n, outL.begin() + done);
        std::copy(bR.begin(), bR.begin() + n, outR.begin() + done);
        done += n;
    }

    // Restore live state.
    rendering_ = false;
    playing_ = wasPlaying; paused_ = wasPaused; curStep_ = savedStep;
    masterVol_ = savedMaster; reverbReturn_ = savedRev; delayReturn_ = savedDly;
    clickOn_ = savedClick;
    for (auto& s : slots_) s.active = false;
    samplePos_ = 0; nextStepSample_ = 0; curStep_ = 0;
    hall_.reset(); echo_.reset(); comp_.reset();
}

void DrumEngine::renderStems(std::vector<StemAudio>& stems, std::vector<std::string>& names) {
    stems.clear(); names.clear();
    std::array<bool, kNumChannels> savedMute{};
    std::array<bool, kNumChannels> savedSolo{};
    for (int i = 0; i < kNumChannels; ++i) { savedMute[i] = channels_[i].muted; savedSolo[i] = channels_[i].solo; }
    for (int ch = 0; ch < kNumChannels; ++ch) {
        for (int i = 0; i < kNumChannels; ++i) { channels_[i].muted = savedMute[i]; channels_[i].solo = savedSolo[i]; }
        bool anyHit = false;
        for (int s = 0; s < steps(); ++s) if (channels_[ch].steps[s] > 0) { anyHit = true; break; }
        if (!anyHit || !isChannelAudible(ch)) continue;
        for (int i = 0; i < kNumChannels; ++i) { channels_[i].muted = (i != ch); channels_[i].solo = false; }
        std::vector<float> l, r;
        renderLoop(4, l, r);
        StemAudio st;
        st.l = std::move(l); st.r = std::move(r);
        stems.push_back(std::move(st));
        names.push_back(kVoices[channels_[ch].voiceIndex].name);
    }
    for (int i = 0; i < kNumChannels; ++i) { channels_[i].muted = savedMute[i]; channels_[i].solo = savedSolo[i]; }
}

// ------------------------------------------------------------ recording
void DrumEngine::startRecording() {
    recording_ = true;
    recordL_.clear(); recordR_.clear();
}

std::shared_ptr<UserSample> DrumEngine::stopRecording(const std::string& name) {
    recording_ = false;
    if (recordL_.empty()) return nullptr;
    auto sm = std::make_shared<UserSample>();
    sm->left = recordL_;
    sm->right = recordR_;
    sm->sampleRate = sampleRate_;
    sm->chopBpm = (int)std::lround(bpm_);
    sm->tempoLock = false;
    sm->name = name;
    sm->valid = true;
    recordL_.clear(); recordR_.clear();
    return sm;
}

} // namespace silktone
