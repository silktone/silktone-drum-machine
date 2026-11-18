# AAX (Pro Tools) — what it takes, and what is still missing

Pro Tools only loads **AAX** plugins. This project can build an AAX bundle
(`Silktone Drum Machine.aaxplugin`), but there are two separate hurdles, and
only the first one is about compiling. The second one — signing — is what
decides whether Joel's retail Pro Tools will actually load it.

## Short version

| Step | Status |
|---|---|
| Build an unsigned `.aaxplugin` (macOS, via this project + GitHub Actions) | Ready — see "Building an unsigned AAX" below |
| Load it in **Pro Tools Developer** (free dev build) | Works once Avid grants a developer license |
| Load it in **retail Pro Tools** | **Blocked** — needs PACE signing (see below) |

An **unsigned** AAX will not appear in retail Pro Tools at all — it is
silently absent from the insert menu (Pro Tools error `-7054`). There is no
workaround for that; signing is mandatory for retail Pro Tools.

## 1. Building the AAX (the compile step — no blockers)

- **Format:** AAX is macOS and Windows 64-bit only. Avid ships no Linux AAX
  SDK, so it cannot be built on this Linux machine at all.
- **SDK:** JUCE 8 vendors a trimmed AAX SDK 2.9.0 inside the JUCE repo
  (`modules/juce_audio_plugin_client/AAX/SDK`), so no separate SDK download
  is needed just to compile. (JUCE requires AAX SDK 2.6.1 or higher.)
  The project's `CMakeLists.txt` has an opt-in switch for it:
  `cmake -S . -B build -G Xcode -DCMAKE_BUILD_TYPE=Release -DSILKTONE_BUILD_AAX=ON`
  If Joel ever gets the full SDK from Avid, pass its path with
  `-DAAX_SDK_PATH=/path/to/aax-sdk` and the build uses that instead.
- **GitHub Actions:** the workflow `.github/workflows/build-macos.yml` has a
  second job, `build-aax`, which runs only when the workflow is started
  manually with the **build_aax** input set to true. It uploads
  `Silktone-Drum-Machine-AAX-UNSIGNED.zip`. It is deliberately not built on
  every push, so nobody mistakes the unsigned file for a finished plugin.
- **Install location (when signed or in Pro Tools Developer):**
  `/Library/Application Support/Avid/Audio/Plug-Ins/` — the system folder,
  not the user `~/Library` folder. Pro Tools only scans the system location.

## 2. Testing the unsigned AAX — Pro Tools Developer (free)

1. Sign up for a free Avid developer account at
   https://developer.avid.com and accept the AAX SDK licence.
2. Email **devauth@avid.com** and request a Pro Tools Developer Bundle
   activation code. (Note: `audiosdk@avid.com` is the wrong address for
   this step — developers report no reply from it; `devauth@avid.com`
   auto-replies with a ~5-day turnaround.)
3. Create an iLok account at https://www.ilok.com and install
   iLok License Manager (the Developer licence is activated there).
4. Download Pro Tools Developer from the Avid developer account and
   install the unsigned `.aaxplugin` in the folder above. It will load
   there — Pro Tools Developer is the only Pro Tools that accepts
   unsigned plugins.

## 3. Retail Pro Tools — PACE signing (the real blocker)

This is a human approval process, not a build step. Nothing here can be
done from this machine until Joel (or Sounds Good Ent) goes through it:

1. **Email Avid** at **audiosdk@avid.com** with the exact subject
   **"PACE Eden Signing Tools Request"**. Include an overview of the
   plugin and a screen recording of it running in Pro Tools Developer
   (with audio if possible). Avid verifies the publisher, then forwards
   the details to PACE.
2. **PACE onboarding:** PACE sends an NDA to sign. Once signed, PACE
   provides the EDEN signing tools (normally ~$500/year; free for
   approved developers, per Avid's process) and a code-signing
   certificate.
3. **iLok USB key:** the signing certificate lives on a physical iLok
   USB key. For CI/cloud signing instead, PACE offers an AAX Cloud
   Signing service (reported at ~$1,000/year) — the certificate is
   hosted by PACE and the workflow signs against it. A physical key
   cannot be plugged into a GitHub runner, so cloud signing (or signing
   on Joel's own Mac) is the only way the GitHub workflow could ever
   produce a signed AAX.
4. **Per-plugin registration:** each plugin gets a unique GUID
   (WCGUID) registered in the PACE developer account.
5. **Sign the bundle** with PACE's `wraptool` (EDEN tools), then
   Apple Developer ID sign + notarize the `.aaxplugin` for macOS
   distribution (separate from the PACE wrap — the bundle needs both).

Only after step 5 will the plugin load in a normal, retail Pro Tools.

## What Joel would need to do

- Create the Avid developer account + iLok account (free).
- Get the Pro Tools Developer licence (free) to test the unsigned build.
- If he wants it in his retail Pro Tools: request the PACE signing
  tools from Avid, get the iLok key (or cloud signing), and sign the
  bundle. That is the only path — no build flag can substitute for it.

## How Joel would use it (once signed)

His Kemper does not host third-party plugins — no AU/VST/AAX installs
on it, only its own profiles and effects. The drum machine runs in the
DAW chain instead: Kemper into Pro Tools on a guitar track, and the
drum machine AAX as an instrument track for the backing groove, or as
a send/insert on the guitar track.

## Sources

- JUCE README (AAX section) and JUCE CMake API docs — AAX needs
  `juce_set_aax_sdk_path` before `juce_add_plugin`; AAX is macOS/Windows
  64-bit only; JUCE 8 vendors AAX SDK 2.9.0.
- Avid / PACE: https://paceap.com/getting-started-with-aax-code-signing-for-pro-tools-plugins/
- JUCE forum, "Releasing AAX plugin — no response from Avid. Tips?"
  (devauth@avid.com vs audiosdk@avid.com, $1,000/yr cloud signing).
