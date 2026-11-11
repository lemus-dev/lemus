# Access Virus C: the firmware on a real 80C515 and on the DSP56362 engine (core/, no JUCE), the
# driver and editor (virusc.cpp, plugin/), and the development tools.

set(VIRUSC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(VIRUSC_ROM ${LEMUS_PRIVATE_DIR}/roms/virusc/virus_c_660x368.bin)
include(${VIRUSC_DIR}/tools/cmake/core.cmake)

juce_add_binary_data(lemus_virusc_fonts HEADER_NAME VirusCFonts.h NAMESPACE VirusCFonts SOURCES
  ${VIRUSC_DIR}/fonts/Michroma-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Medium.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-SemiBold.ttf
)

# The panel's measured layout, read by the editor as it is (docs/panel.json).
juce_add_binary_data(lemus_virusc_panel HEADER_NAME VirusCPanel.h NAMESPACE VirusCPanel SOURCES ${VIRUSC_DIR}/docs/panel.json)

set(VIRUSC_PLUGIN_SOURCES
  ${VIRUSC_DIR}/plugin/PluginProcessor.cpp
  ${VIRUSC_DIR}/plugin/PluginEditor.cpp
  ${VIRUSC_DIR}/plugin/HardwareView.cpp
  ${VIRUSC_DIR}/plugin/Fonts.cpp
)
set(VIRUSC_PLUGIN_INCLUDES ${VIRUSC_DIR}/plugin ${LEMUS_ROOT_DIR} ${LEMUS_MACHINES_DIR})
set_source_files_properties(${VIRUSC_PLUGIN_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${VIRUSC_PLUGIN_INCLUDES}")

lemus_machine(virusc instrument)
lemus_plugin(virusc NAME "ac-digital-vc" CODE Avrc VERSION 0.1.1 STAGE beta STANDALONE MIDI_OUT
  DESCRIPTION "Emulation of the Access Virus C. Not affiliated with Access.")
function(lemus_add_virusc target)
  target_sources(${target} PRIVATE ${VIRUSC_DIR}/virusc.cpp ${VIRUSC_PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE lemus_viruscore lemus_virusc_fonts lemus_virusc_panel)
endfunction()

# The board headless: boot, the LCD as it changes, notes, a WAV at the unit's own rate.
add_executable(viruscrun ${VIRUSC_DIR}/tools/viruscrun/main.cpp)
target_link_libraries(viruscrun PRIVATE lemus_viruscore)

# The check set's bench through the plugin's processor (time, golden, check), and, core only, every
# audio block timed across the factory programs with the deepest stack used (spikes).
juce_add_console_app(viruscbench PRODUCT_NAME "viruscbench")
target_sources(viruscbench PRIVATE ${VIRUSC_DIR}/tools/viruscbench/main.cpp ${VIRUSC_DIR}/tools/viruscbench/plugin.cpp ${VIRUSC_PLUGIN_SOURCES})
target_include_directories(viruscbench PRIVATE ${VIRUSC_PLUGIN_INCLUDES})
target_compile_definitions(viruscbench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 VIRUSC_ROM="${VIRUSC_ROM}")
target_link_libraries(viruscbench PRIVATE lemus_viruscore lemus_virusc_fonts lemus_virusc_panel
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)

# Headless test of the machine's processor. Needs the ROM from the private folder.
juce_add_console_app(viruscplugintest PRODUCT_NAME "viruscplugintest")
target_sources(viruscplugintest PRIVATE ${VIRUSC_DIR}/tools/plugintest/main.cpp ${VIRUSC_PLUGIN_SOURCES})
target_include_directories(viruscplugintest PRIVATE ${VIRUSC_PLUGIN_INCLUDES})
target_compile_definitions(viruscplugintest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 VIRUSC_ROM="${VIRUSC_ROM}")
target_link_libraries(viruscplugintest PRIVATE lemus_viruscore lemus_virusc_fonts lemus_virusc_panel
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
