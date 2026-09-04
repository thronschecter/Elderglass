cmake_minimum_required(VERSION 3.22)

project(VstTemplate VERSION 0.1.0 LANGUAGES CXX)

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
# Plugin target. Change COMPANY_NAME / PLUGIN_MANUFACTURER_CODE / PLUGIN_CODE
# before you ship anything — these become part of the plugin's identity in
# every DAW and can't be changed later without breaking saved sessions.
# ---------------------------------------------------------------------------
juce_add_plugin(VstTemplate
    COMPANY_NAME                "YourCompany"
    PLUGIN_MANUFACTURER_CODE    Ycmp
    PLUGIN_CODE                 Vst1
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "VST Template"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    NEEDS_WEBVIEW2              TRUE
    VST3_CATEGORIES             Fx Filter
)

# Embed the web UI (HTML/CSS/JS) into the binary as C++ resources, so the
# plugin is a single file with no external assets to ship or go missing.
juce_add_binary_data(VstTemplateUIResources
    SOURCES
        UI/index.html
        UI/style.css
        UI/main.js
)
set_target_properties(VstTemplateUIResources PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(VstTemplate PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

target_compile_definitions(VstTemplate PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(VstTemplate
    PRIVATE
        VstTemplateUIResources
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
