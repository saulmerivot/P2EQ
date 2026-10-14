cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel). Must be set before project().
if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
endif()

project(PirateEQ2 VERSION 0.9.3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# static MSVC runtime: testers do not need the Visual C++ Redistributable installed
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE: use the local checkout if present (./JUCE), otherwise download the pinned release (CI)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

if(APPLE)
    set(P2EQ_FORMATS VST3 AU Standalone)
else()
    set(P2EQ_FORMATS VST3 Standalone)
endif()

file(GLOB P2EQ_SKIN_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/skin/*.png")
list(FILTER P2EQ_SKIN_FILES EXCLUDE REGEX "(reference|contact_sheet)\\.png$")
file(GLOB P2EQ_FONT_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf")
list(APPEND P2EQ_SKIN_FILES ${P2EQ_FONT_FILES})
juce_add_binary_data(P2EQSkin HEADER_NAME SkinData.h NAMESPACE SkinData SOURCES ${P2EQ_SKIN_FILES})

juce_add_plugin(PirateEQ2
    PRODUCT_NAME "P2EQ"
    COMPANY_NAME "St. Punk"
    PLUGIN_MANUFACTURER_CODE Stpk
    PLUGIN_CODE Pir2
    FORMATS ${P2EQ_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx EQ
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PirateEQ2 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/BandStrip.cpp
    Source/BandStrip.h
    Source/OldSchool.cpp
    Source/OldSchool.h
    Source/RustTheme.cpp
    Source/RustTheme.h
    Source/Skin.cpp
    Source/Skin.h
    Source/SatPanel.cpp
    Source/SatPanel.h
    Source/dsp/Saturator.h
    Source/Params.h
    Source/dsp/BandFilter.h
    Source/dsp/StreamEQ.h
    Source/dsp/TransientSplit.h)

target_compile_definitions(PirateEQ2 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PirateEQ2
    PRIVATE
        P2EQSkin
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline tool + self-test
juce_add_console_app(SplitTool PRODUCT_NAME "SplitTool")
target_sources(SplitTool PRIVATE Tools/SplitTool.cpp)
target_compile_definitions(SplitTool PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(SplitTool
    PRIVATE
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Offscreen editor render for layout checks
juce_add_console_app(EditorShot PRODUCT_NAME "EditorShot")
target_sources(EditorShot PRIVATE Tools/EditorShot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/BandStrip.cpp Source/OldSchool.cpp Source/RustTheme.cpp Source/Skin.cpp Source/SatPanel.cpp)
target_compile_definitions(EditorShot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(EditorShot
    PRIVATE
        P2EQSkin
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)

# Headless VST3 host for A/B tests against reference plugins
juce_add_console_app(PluginAB PRODUCT_NAME "PluginAB")
target_sources(PluginAB PRIVATE Tools/PluginAB.cpp)
target_compile_definitions(PluginAB PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(PluginAB
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags)

if(WIN32)
# Minimal headless VST2 host (for reference plugins that only exist as VST2)
juce_add_console_app(Vst2AB PRODUCT_NAME "Vst2AB")
target_sources(Vst2AB PRIVATE Tools/Vst2AB.cpp)
target_compile_definitions(Vst2AB PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(Vst2AB
    PRIVATE
        juce::juce_audio_formats
        juce::juce_events
    PUBLIC
        juce::juce_recommended_config_flags)
endif()
