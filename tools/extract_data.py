#!/usr/bin/env python3
"""Extract the Silktone drum machine's data tables from the single-file app
(~/workspace/your_files/drum-machine/drum-machine.html) and emit
Source/SilktoneData.h — the faithful data layer for the JUCE plugin.

Run:  python3 tools/extract_data.py
"""
import re, sys, os, html as htmllib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, '..', 'your_files', 'drum-machine', 'drum-machine.html')
OUT = os.path.join(ROOT, 'Source', 'SilktoneData.h')

html = open(SRC, encoding='utf-8').read()

def grab(pattern, name, flags=re.S):
    m = re.search(pattern, html, flags)
    if not m:
        sys.exit(f'FAILED: could not find {name}')
    return m.group(1)

def esc(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')

def unesc(s):
    return htmllib.unescape(s)

# ---------------- VOICES ----------------
raw = grab(r'var VOICES=\[(.*?)\n\];', 'VOICES')
voices = []
for m in re.finditer(r"\{id:'([^']+)',\s*name:'([^']+)',\s*short:'([^']+)',\s*midi:(\d+)\}", raw):
    voices.append(m.groups())
assert len(voices) == 30, f'voices: {len(voices)}'
voice_ids = {v[0] for v in voices}

# ---------------- KITS ----------------
raw = grab(r'var KITS=\{(.*?)\n\};', 'KITS')
kits = []
for m in re.finditer(r"'([^']+)':\s*\{([^}]*)\}", raw):
    kid, body = m.groups()
    name = re.search(r"name:'([^']+)'", body)
    kv = dict(re.findall(r"(\w+):([\d.\-]+)", body))
    kits.append((kid, name.group(1) if name else kid, kv))
assert len(kits) == 31, f'kits: {len(kits)}'

# kit long descriptions from the kit <select> options ("808 — Deep & Boomy")
kit_desc = {}
m = re.search(r'<select id="kit">(.*?)</select>', html, re.S)
for mm in re.finditer(r'<option value="([^"]+)">([^<]+)</option>', m.group(1)):
    kit_desc[mm.group(1)] = unesc(mm.group(2))

# ---------------- KIT_ORDER ----------------
raw = grab(r'var KIT_ORDER=\[(.*?)\];', 'KIT_ORDER')
order = re.findall(r"'([^']+)'", raw)
assert len(order) == 31, f'kit order: {len(order)}'

# ---------------- FAMILIES ----------------
raw = grab(r'var FAMILIES=\[(.*?)\n\];', 'FAMILIES')
fams = []
for m in re.finditer(r"\{id:'([^']+)',\s*label:'([^']+)',\s*short:'([^']+)',\s*members:\[([^\]]*)\]\}", raw):
    fid, label, short, members = m.groups()
    fams.append((fid, label, short, re.findall(r"'([^']+)'", members)))
assert len(fams) == 9, f'families: {len(fams)}'

# ---------------- SIGS ----------------
raw = grab(r'var SIGS=\{(.*?)\n\};', 'SIGS')
sigs = []
for m in re.finditer(r"'(4/4|3/4|5/4|6/8|7/8|12/8)':\s*\{steps:(\d+),\s*groups:\[([^\]]*)\]\}", raw):
    sigs.append((m.group(1), int(m.group(2)), [int(x) for x in m.group(3).split(',')]))
assert len(sigs) == 6, f'sigs: {len(sigs)}'

# ---------------- PRESET DISPLAY NAMES (from the preset <select>) ----------------
preset_names = {}
m = re.search(r'<select id="preset">(.*?)</select>', html, re.S)
sel = m.group(1)
cur_group = ''
for part in re.split(r'(<optgroup label="[^"]+">|</optgroup>)', sel):
    gm = re.match(r'<optgroup label="([^"]+)">', part or '')
    if gm:
        cur_group = unesc(gm.group(1))
        continue
    if part == '</optgroup>':
        cur_group = ''
        continue
    for mm in re.finditer(r'<option value="([^"]+)">([^<]+)</option>', part or ''):
        preset_names[mm.group(1)] = (unesc(mm.group(2)), cur_group)
assert len(preset_names) == 166, f'preset options: {len(preset_names)}'

# ---------------- PRESETS (flat entries) ----------------
raw = grab(r'var PRESETS=\{(.*?)\n\};', 'PRESETS')
presets = []
i = 0
while i < len(raw):
    m = re.match(r"\s*(\w+):\s*\{", raw[i:])
    if not m:
        i += 1
        continue
    key = m.group(1)
    start = i + m.end() - 1
    depth = 0
    j = start
    while j < len(raw):
        if raw[j] == '{': depth += 1
        elif raw[j] == '}':
            depth -= 1
            if depth == 0: break
        j += 1
    body = raw[start+1:j]
    bpm = re.search(r"bpm:(\d+)", body)
    swing = re.search(r"swing:(\d+)", body)
    swing_int = re.search(r"swingInt:'(\d+)'", body)
    kit = re.search(r"kit:'([^']+)'", body)
    sig = re.search(r"sig:'([^']+)'", body)
    layer = 'layer:true' in body
    rows = {}
    for rm in re.finditer(r"(\w+):'([^']*)'", body):
        k, v = rm.groups()
        if k in voice_ids:
            rows[k] = v
    name, group = preset_names.get(key, (key, ''))
    presets.append((key, name, group,
                    int(bpm.group(1)) if bpm else 0,
                    int(swing.group(1)) if swing else 0,
                    swing_int.group(1) if swing_int else '16',
                    kit.group(1) if kit else '',
                    sig.group(1) if sig else '',
                    layer, rows))
    i = j + 1
assert len(presets) == 166, f'presets: {len(presets)}'

# ---------------- PERC_LOOPS ----------------
raw = grab(r'var PERC_LOOPS=\[(.*?)\n\];', 'PERC_LOOPS')
loops = []
for m in re.finditer(r"\{key:'([^']+)',name:'([^']+)',desc:'([^']+)'\}", raw):
    loops.append(m.groups())
assert len(loops) == 20, f'perc loops: {len(loops)}'

# ---------------- SWING MACHINES ----------------
machines = []
for m in re.finditer(r'data-mpct="(\d+)" data-mname="([^"]+)" data-feel="([^"]+)"', html):
    machines.append(m.groups())
assert len(machines) == 17, f'swing machines: {len(machines)}'

# ---------------- STEM_DEFS ----------------
raw = grab(r'var STEM_DEFS=\[(.*?)\n\];', 'STEM_DEFS')
stems = []
for m in re.finditer(r"\{id:'([^']+)',\s*name:'([^']+)',\s*desc:'([^']+)',\s*pad:'([^']+)'\}", raw):
    stems.append(m.groups())
assert len(stems) == 6, f'stem defs: {len(stems)}'

# ---------------- emit C++ ----------------
lines = []
lines.append('// SilktoneData.h — GENERATED by tools/extract_data.py from')
lines.append('// ~/workspace/your_files/drum-machine/drum-machine.html')
lines.append('// Do not hand-edit: re-run the extractor after any change to the app.')
lines.append('// Counts verified at generation time: 30 voices, 31 kits, 166 presets,')
lines.append('// 20 percussion loops, 17 swing machines, 6 stem definitions, 9 families.')
lines.append('#pragma once')
lines.append('')
lines.append('#include <cstdint>')
lines.append('')
lines.append('namespace silktone {')
lines.append('')
lines.append('struct VoiceDef { const char* id; const char* name; const char* shortName; int midiNote; };')
lines.append('inline constexpr VoiceDef kVoices[] = {')
for v in voices:
    lines.append(f'    {{ "{v[0]}", "{esc(v[1])}", "{v[2]}", {v[3]} }},')
lines.append('};')
lines.append('inline constexpr int kNumVoices = 30;')
lines.append('')
lines.append('struct KitDef {')
lines.append('    const char* id; const char* name; const char* desc;')
lines.append('    // kickF = kick freq, kickD = kick decay, snareT = snare tone,')
lines.append('    // hatD = hat decay, bright = hat brightness, real = acoustic kit flag,')
lines.append('    // muffle/ring/snap/dust/grit/lofi = texture amounts (0 when absent).')
lines.append('    double kickF, kickD, snareT, hatD, bright, real, muffle, ring, snap, dust, grit, lofi;')
lines.append('};')
lines.append('inline constexpr KitDef kKits[] = {')
for kid, name, kv in kits:
    def g(k, d=0.0):
        return float(kv.get(k, d))
    desc = kit_desc.get(kid, '')
    lines.append(f'    {{ "{kid}", "{esc(name)}", "{esc(desc)}", {g("kickF",150):g}, {g("kickD",0.4):g}, {g("snareT",190):g}, {g("hatD",1.0):g}, '
                 f'{g("bright",1):g}, {g("real",0):g}, {g("muffle",0):g}, {g("ring",0):g}, {g("snap",0):g}, '
                 f'{g("dust",0):g}, {g("grit",0):g}, {g("lofi",0):g} }},')
lines.append('};')
lines.append('inline constexpr int kNumKits = 31;')
lines.append('')
lines.append('inline constexpr const char* kKitOrder[] = {')
for kid in order:
    lines.append(f'    "{kid}",')
lines.append('};')
lines.append('')
lines.append('struct FamilyDef { const char* id; const char* label; const char* shortLabel; const char* members[5]; int numMembers; };')
lines.append('inline constexpr FamilyDef kFamilies[] = {')
for fid, label, short, members in fams:
    mem = ', '.join(f'"{m}"' for m in members)
    pad = ', '.join(['nullptr'] * (5 - len(members)))
    allm = mem + (', ' + pad if pad else '')
    lines.append(f'    {{ "{fid}", "{esc(label)}", "{esc(short)}", {{ {allm} }}, {len(members)} }},')
lines.append('};')
lines.append('inline constexpr int kNumFamilies = 9;')
lines.append('')
lines.append('struct SigDef { const char* name; int steps; int groups[5]; int numGroups; };')
lines.append('inline constexpr SigDef kSigs[] = {')
for name, steps, groups in sigs:
    g = ', '.join(str(x) for x in groups) + ', ' + ', '.join(['0'] * (5 - len(groups)))
    lines.append(f'    {{ "{name}", {steps}, {{ {g} }}, {len(groups)} }},')
lines.append('};')
lines.append('inline constexpr int kNumSigs = 6;')
lines.append('')
lines.append('struct PresetDef {')
lines.append('    const char* key; const char* name; const char* group;')
lines.append('    int bpm; int swing; const char* swingInt; const char* kit; const char* sig;')
lines.append('    bool layer;  // percussion loops layer over the current pattern')
lines.append('    // rows[voiceIndex]: "X" = accent, "x" = hit, "." = rest; nullptr when absent.')
lines.append('    // Strings tile/truncate to the bar length, exactly as parsePat() does.')
lines.append('    const char* rows[30];')
lines.append('};')
lines.append('inline constexpr PresetDef kPresets[] = {')
for key, name, group, bpm, swing, swing_int, kit, sig, layer, rows in presets:
    rowvals = []
    for v in voices:
        s = rows.get(v[0])
        rowvals.append(f'"{esc(s)}"' if s is not None else 'nullptr')
    lines.append(f'    {{ "{key}", "{esc(name)}", "{esc(group)}", {bpm}, {swing}, "{swing_int}", "{kit}", "{sig}", {"true" if layer else "false"},')
    lines.append('      { ' + ', '.join(rowvals) + ' } },')
lines.append('};')
lines.append('inline constexpr int kNumPresets = 166;')
lines.append('')
lines.append('struct PercLoopDef { const char* key; const char* name; const char* desc; };')
lines.append('inline constexpr PercLoopDef kPercLoops[] = {')
for key, name, desc in loops:
    lines.append(f'    {{ "{key}", "{esc(name)}", "{esc(desc)}" }},')
lines.append('};')
lines.append('inline constexpr int kNumPercLoops = 20;')
lines.append('')
lines.append('struct SwingMachineDef { const char* name; int percent; const char* feel; };')
lines.append('inline constexpr SwingMachineDef kSwingMachines[] = {')
for pct, name, feel in machines:
    lines.append(f'    {{ "{esc(name)}", {pct}, "{esc(feel)}" }},')
lines.append('};')
lines.append('inline constexpr int kNumSwingMachines = 17;')
lines.append('')
lines.append('struct StemDef { const char* id; const char* name; const char* desc; const char* padVoice; };')
lines.append('inline constexpr StemDef kStemDefs[] = {')
for sid, name, desc, pad in stems:
    lines.append(f'    {{ "{sid}", "{esc(name)}", "{esc(desc)}", "{pad}" }},')
lines.append('};')
lines.append('inline constexpr int kNumStemDefs = 6;')
lines.append('')
lines.append('} // namespace silktone')
lines.append('')

open(OUT, 'w', encoding='utf-8').write('\n'.join(lines))
print(f'Wrote {OUT}')
print(f'voices={len(voices)} kits={len(kits)} presets={len(presets)} '
      f'loops={len(loops)} machines={len(machines)} stems={len(stems)} fams={len(fams)} sigs={len(sigs)}')
