#!/usr/bin/env python3
"""Verification pass for the Silktone drum machine plugin project.
Checks, from this Linux VM (no macOS/Xcode/JUCE):
  1. every file named in CMakeLists.txt exists
  2. every #include "X" in Source/ resolves to an existing file
  3. SilktoneData.h matches a fresh regeneration from the app HTML
  4. plugin IDs / manufacturer codes are valid (4 chars, distinct namespaces)
  5. README build paths match the actual layout
  6. feature checklist: every app feature has an implementation
  7. manual PDF exists, is Letter, has 8 pages
  8. DSP test passes
Prints PASS/FAIL per check and a summary. Exits non-zero on any FAIL.
"""
import os, re, subprocess, sys

ROOT = os.path.expanduser("~/workspace/drum-machine-plugin")
APP = os.path.expanduser("~/workspace/your_files/drum-machine/drum-machine.html")
fails, passes = [], []

def check(name, ok, detail=""):
    (passes if ok else fails).append(name)
    print(("PASS " if ok else "FAIL ") + name + ((" — " + detail) if detail else ""))

def read(p):
    with open(p) as f:
        return f.read()

# --- 1. CMake file list -----------------------------------------------------
cmake = read(os.path.join(ROOT, "CMakeLists.txt"))
listed = re.findall(r"target_sources\(SilktoneDrumMachine PRIVATE(.*?)\n\)", cmake, re.S)
listed_files = re.findall(r"(\S+\.(?:cpp|h))\b", listed[0]) if listed else []
check("CMakeLists.txt lists sources", len(listed_files) > 0, f"{len(listed_files)} files")
missing = [f for f in listed_files if not os.path.exists(os.path.join(ROOT, f))]
check("all CMake-listed sources exist", not missing, ", ".join(missing) or "all present")
test_listed = "Tests/DspTest.cpp" in cmake
check("DSP test is in CMake", test_listed)
for f in ["Tests/DspTest.cpp", "tools/extract_data.py", "README.md",
          "docs/USER_MANUAL.md", "docs/Silktone-Drum-Machine-Manual.pdf"]:
    check(f"exists: {f}", os.path.exists(os.path.join(ROOT, f)))

# --- 2. #include resolution --------------------------------------------------
src = os.path.join(ROOT, "Source")
for fname in sorted(os.listdir(src)):
    if not fname.endswith((".cpp", ".h")):
        continue
    body = read(os.path.join(src, fname))
    for inc in re.findall(r'#include "([^"]+)"', body):
        check(f"include resolves: {fname} -> {inc}",
              os.path.exists(os.path.join(src, inc)))

# --- 3. Data header matches a fresh regeneration ----------------------------
# extract_data.py takes no arguments: it reads the app HTML and rewrites
# Source/SilktoneData.h in place. So: hash before, run, hash after.
import hashlib
def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()
header_path = os.path.join(src, "SilktoneData.h")
before = sha(header_path)
gen = subprocess.run(
    ["python3", os.path.join(ROOT, "tools/extract_data.py")],
    capture_output=True, text=True)
check("extract_data.py runs clean", gen.returncode == 0,
      (gen.stdout.strip() + " " + gen.stderr.strip())[:200])
after = sha(header_path)
check("SilktoneData.h matches regeneration (deterministic)",
      before == after, "header changed on regeneration" if before != after else "identical")

data = read(os.path.join(src, "SilktoneData.h"))
for label, pat, expect in [
    ("voices", r"inline constexpr VoiceDef kVoices\[\] = \{", 30),
    ("kits", r"inline constexpr KitDef kKits\[\] = \{", 31),
    ("perc loops", r"inline constexpr PercLoopDef kPercLoops\[\] = \{", 20),
    ("swing machines", r"inline constexpr SwingMachineDef kSwingMachines\[\] = \{", 17),
    ("stem defs", r"inline constexpr StemDef kStemDefs\[\] = \{", 6),
    ("families", r"inline constexpr FamilyDef kFamilies\[\] = \{", 9),
    ("time sigs", r"inline constexpr SigDef kSigs\[\] = \{", 6),
]:
    m = re.search(pat + r"(.*?)\n\};", data, re.S)
    n = len(re.findall(r"^\s*\{", m.group(1), re.M)) if m else -1
    check(f"data count: {label} == {expect}", n == expect, f"found {n}")
# Presets span two lines each (entry line + rows line), so count by the
# layer flag that ends each entry line instead of by leading braces.
m = re.search(r"inline constexpr PresetDef kPresets\[\] = \{(.*?)\n\};", data, re.S)
n = len(re.findall(r", (?:true|false),\n", m.group(1))) if m else -1
check("data count: presets == 166", n == 166, f"found {n}")

# --- 4. Plugin IDs ----------------------------------------------------------
def cmake_val(key):
    m = re.search(key + r"\s+(\S+)", cmake)
    return m.group(1) if m else None

mfr = cmake_val("PLUGIN_MANUFACTURER_CODE")
code = cmake_val("PLUGIN_CODE")
bundle = cmake_val("BUNDLE_ID")
check("manufacturer code is 4 chars", bool(mfr) and len(mfr) == 4, str(mfr))
check("plugin code is 4 chars", bool(code) and len(code) == 4, str(code))
check("manufacturer != plugin code", mfr != code, f"{mfr} vs {code}")
check("manufacturer code is alphanumeric", bool(mfr) and mfr.isalnum())
check("plugin code is alphanumeric", bool(code) and code.isalnum())
check("bundle ID is valid reverse-DNS",
      bool(bundle) and re.fullmatch(r"[a-z0-9]+(\.[a-z0-9-]+)+", bundle) is not None, str(bundle))
check("AU main type is aumu", "AU_MAIN_TYPE aumu" in cmake)
check("formats are AU VST3 Standalone", "FORMATS AU VST3 Standalone" in cmake)
check("IS_SYNTH TRUE (instrument in Logic)", "IS_SYNTH TRUE" in cmake)
check("MIDI in/out declared", "NEEDS_MIDI_INPUT TRUE" in cmake and "NEEDS_MIDI_OUTPUT TRUE" in cmake)
check("JUCE version pinned", "GIT_TAG 8.0.6" in cmake)
check("no installer / no copy-after-build", "COPY_PLUGIN_AFTER_BUILD FALSE" in cmake)

# --- 5. README matches layout ----------------------------------------------
readme = read(os.path.join(ROOT, "README.md"))
for path in ["CMakeLists.txt", "Source/PluginProcessor.cpp", "Source/PluginEditor.cpp",
             "Source/DrumEngine.cpp", "Source/DspCore.cpp", "Source/SilktoneData.h",
             "Tests/DspTest.cpp", "tools/extract_data.py", "docs/USER_MANUAL.md",
             "docs/Silktone-Drum-Machine-Manual.pdf"]:
    check(f"README path exists: {path}", os.path.exists(os.path.join(ROOT, path)))
check("README names the AU install folder",
      "~/Library/Audio/Plug-Ins/Components/" in readme)
check("README names the VST3 install folder",
      "~/Library/Audio/Plug-Ins/VST3/" in readme)
check("README names the artefacts folder",
      "SilktoneDrumMachine_artefacts/Release" in readme)
check("README states no binary exists yet",
      "no compiled" in readme.lower() and ".component" in readme)

# --- 6. Feature checklist (app -> plugin) -----------------------------------
engine = read(os.path.join(src, "DrumEngine.cpp")) + read(os.path.join(src, "DrumEngine.h"))
proc = read(os.path.join(src, "PluginProcessor.cpp"))
editor = read(os.path.join(src, "PluginEditor.cpp"))
look = read(os.path.join(src, "LookAndFeel.cpp"))
features = [
    ("30 voices / sequencer steps", "steps" in engine and "kVoices" in data),
    ("accents (step value 2)", "steps[step_] = next" in editor or "== 2" in engine),
    ("mute/solo per row", "anyChannelSolo" in engine),
    ("sound scrolling (family)", "scrollChannelSound" in engine and "scrollChannelSound" in editor),
    ("copy/paste rows", "copyRow" in engine and "pasteRow" in editor),
    ("swing machines in UI", "applySwingMachine" in editor and "swingMachineBox_" in editor),
    ("mixer fader per voice", "volAttachment_" in editor),
    ("pan / tone / sat knobs", "panKnob_" in editor and "toneKnob_" in editor and "satKnob_" in editor),
    ("two sends per channel", "revKnob_" in editor and "dlyKnob_" in editor),
    ("aux mute/solo", "auxMuteButton_" in editor and "auxSoloButton_" in editor),
    ("reverb + echo returns", "revReturn" in proc and "dlyReturn" in proc),
    ("click", "setClick" in proc and "click" in editor),
    ("tap tempo", "tapTempo" in editor),
    ("transport play/pause/stop", "togglePlay" in engine or "play()" in editor),
    ("sampler: load file", "loadSampleOntoChannel" in proc),
    ("sampler: record from sidechain", "Record Input" in proc and "startRecording" in proc),
    ("tempo-locked auto-chop", "autoChop" in engine and "chopSelected" in editor),
    ("slices to pads", "assignSliceToVoice" in editor),
    ("library scan/search", "scanFolder" in proc and "searchBox_" in editor),
    ("favorites (persisted in state)", "favorite" in proc and "favorite" in editor),
    ("stem splitter (filter split)", "splitStems" in engine and "splitStems_" in proc),
    ("stem to pad", "sendStemToPad" in proc),
    ("MIDI export", "buildMidi" in engine and "exportMidi" in proc),
    ("WAV loop export", "renderLoop" in engine and "exportLoopWav" in proc),
    ("per-voice stems ZIP", "renderStems" in engine and "exportStemsZip" in proc),
    ("MIDI out (channel 10)", "takeMidiOutEvents" in proc),
    ("host tempo/transport sync", "setHostBpm" in proc and "followHost" in proc),
    ("info menu", "showInfo" in editor),
    ("70s look (browns/yellows/wood)", "woodDark" in look and "amber" in look),
    ("state save/load with samples", "sampleWav" in proc),
    ("no gain knob (dropped by Joel)", "gain" not in editor.lower().replace("again", "")),
]
for name, ok in features:
    check("feature: " + name, bool(ok))

# --- 7. Manual PDF -----------------------------------------------------------
pdf = os.path.join(ROOT, "docs/Silktone-Drum-Machine-Manual.pdf")
info = subprocess.run(["pdfinfo", pdf], capture_output=True, text=True).stdout
pages = re.search(r"Pages:\s+(\d+)", info)
check("manual PDF is Letter", "Page size:       612 x 792 pts (letter)" in info, info.splitlines()[2] if info else "")
check("manual PDF has 8 pages", pages and pages.group(1) == "8", pages.group(1) if pages else "?")

# --- 8. DSP test -------------------------------------------------------------
build = subprocess.run(
    ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-I", src,
     "-o", "/tmp/SilktoneDspTest-verify",
     os.path.join(ROOT, "Tests/DspTest.cpp"),
     os.path.join(src, "DspCore.cpp"), os.path.join(src, "DrumEngine.cpp")],
    capture_output=True, text=True)
check("DSP test compiles (-O2, -Wall -Wextra)",
      build.returncode == 0, build.stderr.strip()[:300])
if build.returncode == 0:
    run = subprocess.run(["/tmp/SilktoneDspTest-verify"], capture_output=True, text=True)
    m = re.search(r"RESULT: (PASS|FAIL)", run.stdout)
    check("DSP test passes", m and m.group(1) == "PASS", run.stdout.strip().splitlines()[-1] if run.stdout else "")

print(f"\n{len(passes)} passed, {len(fails)} failed")
if fails:
    print("FAILURES:")
    for f in fails:
        print("  - " + f)
sys.exit(1 if fails else 0)
