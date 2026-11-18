# Silktone Drum Machine — JUCE plugin project (macOS)
#
# Build on a Mac with Xcode (see README.md):
#   cmake -S . -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
# or from the command line without Xcode's GUI:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# The built bundles land in build/SilktoneDrumMachine_artefacts/Release/:
#   Silktone Drum Machine.component  -> ~/Library/Audio/Plug-Ins/Components/
#   Silktone Drum Machine.vst3       -> ~/Library/Audio/Plug-Ins/VST3/
#   Silktone Drum Machine.app        -> Standalone (double-click to run)
#
# This project cannot be built on Linux: the AU and VST3 wrappers need the
# macOS SDK and Xcode. The DSP core and engine (Source/DspCore.cpp,
# Source/DrumEngine.cpp) are platform-independent and are covered by the
# standalone test in Tests/DspTest.cpp, which DOES build on Linux.

cmake_minimum_required(VERSION 3.22)
project(SilktoneDrumMachine VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Minimum macOS for Joel's machine (Monterey 12.7.6). NOTE: this line alone
# does NOT take effect with the Xcode generator — the value set here after
# project() was silently ignored and the 2026-11-17 builds shipped with
# minos 15.5. The workflow MUST also pass -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
# on the cmake command line (set before project(), so the Xcode generator
# uses it as the project-wide default). This line keeps local builds honest.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")

# ------------------------------------------------------------------- JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# ------------------------------------------------- AAX (Pro Tools) — opt-in
# AAX is macOS/Windows 64-bit only (Avid ships no Linux SDK) and the bundle
# it produces is UNSIGNED: retail Pro Tools will not load it until it is
# PACE-signed. See docs/AAX.md for the full picture. Default OFF.
# Verified against JUCE 8.0.6: JUCE auto-uses its vendored AAX SDK 2.9.0
# (modules/juce_audio_plugin_client/AAX/SDK) when an AAX target is created,
# so no explicit SDK path is needed to compile. On Linux, enabling this is
# a hard error — JUCE cannot create the AAX target there at all.
option(SILKTONE_BUILD_AAX "Build the AAX (Pro Tools) target — unsigned; loads only in Pro Tools Developer until PACE-signed" OFF)
set(SILKTONE_AAX_SDK_PATH "" CACHE PATH "Optional path to a full Avid AAX SDK; if empty, JUCE 8's vendored AAX SDK 2.9.0 is used")

set(SILKTONE_FORMATS AU VST3 Standalone)
if(SILKTONE_BUILD_AAX)
    if(NOT APPLE AND NOT WIN32)
        message(FATAL_ERROR "SILKTONE_BUILD_AAX=ON is only supported on macOS and Windows — Avid ships no Linux AAX SDK.")
    endif()
    if(SILKTONE_AAX_SDK_PATH)
        juce_set_aax_sdk_path(${SILKTONE_AAX_SDK_PATH})
    endif()
    list(APPEND SILKTONE_FORMATS AAX)
endif()

# ------------------------------------------------------------------- plugin
juce_add_plugin(SilktoneDrumMachine
    PRODUCT_NAME "Silktone Drum Machine"
    COMPANY_NAME "Sounds Good Ent"
    COMPANY_WEBSITE "https://soundsgoodent.example"  # placeholder — no site yet
    BUNDLE_ID com.soundsgoodent.silktone-drummachine
    PLUGIN_MANUFACTURER_CODE Sltn
    PLUGIN_CODE SLM8
    FORMATS ${SILKTONE_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice  # JUCE 8 wants the kAudioUnitType_* name, not the four-char code (aumu left JUCE_AU_MAIN_TYPE_CODE unset and the AU build failed)
    VST3_CATEGORIES Instrument
    AAX_CATEGORY AAX_ePlugInCategory_SWGenerators
    AU_EXPORT_PREFIX SilktoneDrumMachineAU
    COPY_PLUGIN_AFTER_BUILD FALSE
    DESCRIPTION "Silktone drum machine: 30 voices, 16-step sequencer, pads, mixer, sampler, library, stem splitter."
)

# Generate JuceHeader.h — required: PluginProcessor.h, PluginEditor.h and
# LookAndFeel.h all #include <JuceHeader.h>. juce_add_plugin does NOT
# generate it on its own (only juce_add_pip does). Without this line the
# plugin target fails to compile on every platform with
# "fatal error: JuceHeader.h: No such file or directory" — caught by the
# Linux smoke build on 2026-11-17, before any macOS build was attempted.
juce_generate_juce_header(SilktoneDrumMachine)

target_sources(SilktoneDrumMachine PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/LookAndFeel.cpp
    Source/LookAndFeel.h
    Source/DspCore.cpp
    Source/DspCore.h
    Source/DrumEngine.cpp
    Source/DrumEngine.h
    Source/SilktoneData.h
)

target_compile_definitions(SilktoneDrumMachine PRIVATE
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(SilktoneDrumMachine PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_core
)

# ------------------------------------------------- standalone DSP test (any OS)
# The DSP core and engine have no JUCE dependency, so this test builds and
# runs on Linux too — it is the only part of the project verifiable here.
option(SILKTONE_BUILD_TESTS "Build the standalone DSP test" ON)
if(SILKTONE_BUILD_TESTS)
    add_executable(SilktoneDspTest
        Tests/DspTest.cpp
        Source/DspCore.cpp
        Source/DrumEngine.cpp
    )
    target_include_directories(SilktoneDspTest PRIVATE Source)
    target_compile_definitions(SilktoneDspTest PRIVATE SILKTONE_DSP_TEST=1)
    enable_testing()
    add_test(NAME SilktoneDspTest COMMAND SilktoneDspTest)
endif()
