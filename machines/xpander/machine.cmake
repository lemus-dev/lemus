# Oberheim Xpander: the main and voice CPU boards running the real firmware (main 1.2, voice 1.4),
# the six analog voices (CEM3374, CEM3372, pole mixing), the output section (core/, no JUCE); the
# driver and editor (xpander.cpp, plugin/); the tools.

set(XPANDER_DIR ${CMAKE_CURRENT_LIST_DIR})
set(XPANDER_ROM_DIR ${LEMUS_PRIVATE_DIR}/roms/xpander)
include(${XPANDER_DIR}/tools/cmake/core.cmake)

# The panel: docs/panel.json built in, its print in Nimbus Sans Narrow (fonts/README.md), the bar in
# the host's IBM Plex Mono.
juce_add_binary_data(lemus_xpander_fonts HEADER_NAME XpanderFonts.h NAMESPACE XpanderFonts SOURCES
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Medium.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-SemiBold.ttf
  ${XPANDER_DIR}/fonts/NimbusSansNarrow-Regular.ttf
  ${XPANDER_DIR}/fonts/NimbusSansNarrow-Bold.ttf
)
juce_add_binary_data(lemus_xpander_panel HEADER_NAME XpanderPanel.h NAMESPACE XpanderPanel SOURCES ${XPANDER_DIR}/docs/panel.json)

set(XPANDER_PLUGIN_SOURCES
  ${XPANDER_DIR}/plugin/PluginProcessor.cpp
  ${XPANDER_DIR}/plugin/PluginEditor.cpp
  ${XPANDER_DIR}/plugin/HardwareView.cpp
)
set(XPANDER_PLUGIN_INCLUDES ${XPANDER_DIR}/plugin ${LEMUS_ROOT_DIR} ${LEMUS_MACHINES_DIR})
set_source_files_properties(${XPANDER_PLUGIN_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${XPANDER_PLUGIN_INCLUDES}")

lemus_machine(xpander instrument)
lemus_plugin(xpander NAME "ob-poly-xp" CODE Oxpp VERSION 0.1.0 STAGE beta STANDALONE MIDI_OUT
  DESCRIPTION "Emulation of the Oberheim Xpander. Not affiliated with Oberheim.")
function(lemus_add_xpander target)
  target_sources(${target} PRIVATE ${XPANDER_DIR}/xpander.cpp ${XPANDER_PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE lemus_xpandercore lemus_xpander_fonts lemus_xpander_panel)
endfunction()

# The firmware headless: boots, presses keys, prints the displays (tools/fwrun).
add_executable(xpanderfw ${XPANDER_DIR}/tools/fwrun/main.cpp)
target_link_libraries(xpanderfw PRIVATE lemus_xpandercore)

# Renders a scripted performance headless (tools/render).
add_executable(xpanderrender ${XPANDER_DIR}/tools/render/main.cpp)
target_link_libraries(xpanderrender PRIVATE lemus_xpandercore)

# Headless test of the machine's processor (tools/plugintest; --panel <dir> renders the editor too).
juce_add_console_app(xpanderplugintest PRODUCT_NAME "xpanderplugintest")
target_sources(xpanderplugintest PRIVATE ${XPANDER_DIR}/tools/plugintest/main.cpp ${XPANDER_PLUGIN_SOURCES})
target_include_directories(xpanderplugintest PRIVATE ${XPANDER_PLUGIN_INCLUDES})
target_compile_definitions(xpanderplugintest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 XPANDER_ROM_DIR="${XPANDER_ROM_DIR}")
target_link_libraries(xpanderplugintest PRIVATE lemus_xpandercore lemus_xpander_fonts lemus_xpander_panel
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)

# The workloads: cost, golden renders, checks against them (tools/bench).
add_executable(xpanderbench ${XPANDER_DIR}/tools/bench/main.cpp)
target_link_libraries(xpanderbench PRIVATE lemus_xpandercore)
target_compile_definitions(xpanderbench PRIVATE XPANDER_ROM_DIR="${XPANDER_ROM_DIR}")

# The controls' extremes: unconverged solves, noise where nothing makes it (tools/corners).
juce_add_console_app(xpandercorners PRODUCT_NAME "xpandercorners")
target_sources(xpandercorners PRIVATE ${XPANDER_DIR}/tools/corners/main.cpp ${XPANDER_PLUGIN_SOURCES})
target_include_directories(xpandercorners PRIVATE ${XPANDER_PLUGIN_INCLUDES})
target_compile_definitions(xpandercorners PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 XPANDER_ROM_DIR="${XPANDER_ROM_DIR}")
target_link_libraries(xpandercorners PRIVATE lemus_xpandercore lemus_xpander_fonts lemus_xpander_panel
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)

# The model's blocks alone, held against ngspice by tools/spice/suite.py.
add_executable(xpandercircuitrun ${XPANDER_DIR}/tools/circuitrun/main.cpp)
target_link_libraries(xpandercircuitrun PRIVATE lemus_xpandercore)
