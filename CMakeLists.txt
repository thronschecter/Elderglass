cmake_minimum_required(VERSION 3.22)

project(Elderglass VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# JUCE is fetched automatically on first configure — no manual SDK install,
# no submodule. Pin the tag when you want to move to a newer JUCE release.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Static WebView2 loader, needed for the UI (see NEEDS_WEBVIEW2 below). JUCE
# looks for it as a NuGet-style "Microsoft.Web.WebView2.<version>" folder;
# fetch it directly instead of requiring a manual `Install-Package` step.
# ---------------------------------------------------------------------------
if(WIN32)
    set(webview2_version "1.0.3485.44")
    set(webview2_nuget_dir "${CMAKE_BINARY_DIR}/_deps/nuget")

    FetchContent_Declare(
        webview2
        URL                        "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${webview2_version}"
        DOWNLOAD_NAME              "webview2.zip"
        SOURCE_DIR                 "${webview2_nuget_dir}/Microsoft.Web.WebView2.${webview2_version}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(webview2)

    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${webview2_nuget_dir}" CACHE PATH
        "Location of the WebView2 NuGet package" FORCE)

    # Steinberg ASIO SDK, used under its GPLv3 option, so the Standalone app
    # can open ASIO drivers. Only the headers in common/ are needed.
    FetchContent_Declare(
        asiosdk
        URL                        "https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip"
        URL_HASH                   SHA256=D5EBF0C20DD2C5F43771FD0C1418F4B361BF52434EE670097CFA6B3A335E2ECA
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(asiosdk)
endif()

# ---------------------------------------------------------------------------
# Plugin target. Change COMPANY_NAME / PLUGIN_MANUFACTURER_CODE / PLUGIN_CODE
# before you ship anything — these become part of the plugin's identity in
# every DAW and can't be changed later without breaking saved sessions.
# ---------------------------------------------------------------------------
juce_add_plugin(Elderglass
    COMPANY_NAME                "Barking Noise Studios"
    PLUGIN_MANUFACTURER_CODE    Bkns
    PLUGIN_CODE                 Eldg
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "Elderglass"
    BUNDLE_ID                   com.barkingnoisestudios.elderglass
    # The tree on a sage tile, drawn by Tools/make_icon.py.
    ICON_BIG                    Resources/icon.png
    IS_SYNTH                    FALSE
    # MIDI CC control and MIDI learn (see Source/MidiLearn.h).
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    # Off by default: the system VST3 folder (C:\Program Files\Common
    # Files\VST3) needs admin rights to write to, and a non-elevated build
    # would fail here. Flip to TRUE (and build elevated) once you want
    # every build auto-installed for your DAW — see README.md.
    COPY_PLUGIN_AFTER_BUILD     FALSE
    NEEDS_WEBVIEW2              TRUE
    # Linux: links WebKitGTK for the UI, and embeds the helper process the
    # plugin needs to host it.
    NEEDS_WEB_BROWSER           TRUE
    VST3_CATEGORIES             Fx Delay
    # JUCE normally runs a small generated helper .exe post-build to write
    # moduleinfo.json (an optional fast-scan manifest — hosts fall back to
    # normal plugin scanning without it). On machines with Device
    # Guard/WDAC or similar app-control policies, running a freshly built,
    # unsigned helper executable is blocked, which aborts the whole
    # post-build step. Skip it; set this back to the default (omit it)
    # once you're building on/for a machine that allows it.
    VST3_AUTO_MANIFEST          FALSE
)

# Embed the web UI (HTML/CSS/JS) into the binary as C++ resources, so the
# plugin is a single file with no external assets to ship or go missing.
# JUCE's JS side of the parameter relays is embedded as well and served
# under "juce/" (see PluginEditor::getResource).
set(juce_js_dir "${juce_SOURCE_DIR}/modules/juce_gui_extra/native/javascript")
juce_add_binary_data(ElderglassUIResources
    SOURCES
        UI/index.html
        UI/style.css
        UI/main.js
        UI/art.js
        UI/art-data.js
        UI/glass.js
        "${juce_js_dir}/index.js"
        "${juce_js_dir}/check_native_interop.js"
)
set_target_properties(ElderglassUIResources PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Elderglass PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Parameters.cpp
    Source/Parameters.h
    Source/MidiLearn.cpp
    Source/MidiLearn.h
    Source/DSP/DspUtils.h
    Source/DSP/ClockedResampler.h
    Source/DSP/DelayStage.h
    Source/DSP/GrainStage.h
    Source/DSP/FdnReverb.h
    Source/DSP/PitchShifter.h
    Source/DSP/PedalModel.h
    Source/DSP/PedalCore.h
    Source/DSP/GranularModel.h
    Source/DSP/ReverseModel.h
)

target_compile_definitions(Elderglass PUBLIC
    JUCE_WEB_BROWSER=1
    $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>
)

if(WIN32)
    target_include_directories(Elderglass PRIVATE "${asiosdk_SOURCE_DIR}/common")
endif()

target_link_libraries(Elderglass
    PRIVATE
        ElderglassUIResources
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Every Release build also lands in Builds/ at the top of the repo, so the
# current plugin is easy to find (and is committed with the source). The
# Windows and Linux builds share one VST3 bundle (Contents/x86_64-win and
# Contents/x86_64-linux); the standalone app is Elderglass.exe / Elderglass.
# ---------------------------------------------------------------------------
set(elderglass_builds_dir "${CMAKE_SOURCE_DIR}/Builds")
get_target_property(elderglass_vst3_bundle Elderglass_VST3 JUCE_PLUGIN_ARTEFACT_FILE)

add_custom_command(TARGET Elderglass_VST3 POST_BUILD
    COMMAND "$<IF:$<CONFIG:Release>,${CMAKE_COMMAND};-E;copy_directory;${elderglass_vst3_bundle};${elderglass_builds_dir}/Elderglass.vst3,${CMAKE_COMMAND};-E;true>"
    COMMAND_EXPAND_LISTS
    VERBATIM)

add_custom_command(TARGET Elderglass_Standalone POST_BUILD
    COMMAND "$<IF:$<CONFIG:Release>,${CMAKE_COMMAND};-E;copy;$<TARGET_FILE:Elderglass_Standalone>;${elderglass_builds_dir}/$<TARGET_FILE_NAME:Elderglass_Standalone>,${CMAKE_COMMAND};-E;true>"
    COMMAND_EXPAND_LISTS
    VERBATIM)

# ---------------------------------------------------------------------------
# Offline DSP checks (plain console app, no JUCE): stability, latency and
# transparency of the pedal engines. Run build/<config>/ElderglassDspTest.exe
# (Linux: build-linux/ElderglassDspTest).
# ---------------------------------------------------------------------------
add_executable(ElderglassDspTest Tests/DspTest.cpp)

# Reverb impulse-response analysis (echo density, periodicity). Informational.
add_executable(ElderglassReverbAnalysis Tests/ReverbAnalysis.cpp)
