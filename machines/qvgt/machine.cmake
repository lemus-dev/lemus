# Alesis QuadraVerb GT: the firmware on an 80C31 (MAME's mcs51 core) with the DASP 24 effects DSP as
# decoded from it, and the converters' and preamp's analog stages (core/, no JUCE), the driver and
# editor (qvgt.cpp, plugin/), and the development tools.

set(QVGT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(QVGT_ROM ${LEMUS_PRIVATE_DIR}/roms/qvgt/qvgtv103.bin)
set(QVGT_LCD_ROM ${LEMUS_PRIVATE_DIR}/roms/qvgt/hd44780_a00.bin)
include(${QVGT_DIR}/tools/cmake/core.cmake)

juce_add_binary_data(lemus_qvgt_fonts HEADER_NAME QVGTFonts.h NAMESPACE QVGTFonts SOURCES
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Medium.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-SemiBold.ttf
)

juce_add_binary_data(lemus_qvgt_panel HEADER_NAME QVGTPanel.h NAMESPACE QVGTPanel SOURCES ${QVGT_DIR}/docs/panel.json)

set(QVGT_PLUGIN_SOURCES
  ${QVGT_DIR}/plugin/PluginProcessor.cpp
  ${QVGT_DIR}/plugin/PluginEditor.cpp
  ${QVGT_DIR}/plugin/HardwareView.cpp
  ${QVGT_DIR}/plugin/Fonts.cpp
)
set(QVGT_PLUGIN_INCLUDES ${QVGT_DIR}/plugin ${LEMUS_ROOT_DIR} ${LEMUS_MACHINES_DIR})
set_source_files_properties(${QVGT_PLUGIN_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${QVGT_PLUGIN_INCLUDES}")

lemus_machine(qvgt fx)
lemus_plugin(qvgt NAME "al-fx-quadgt" CODE Aqvg VERSION 0.1.2 STAGE beta STANDALONE MIDI_OUT
  DESCRIPTION "Emulation of the Alesis QuadraVerb GT. Not affiliated with Alesis.")
function(lemus_add_qvgt target)
  target_sources(${target} PRIVATE ${QVGT_DIR}/qvgt.cpp ${QVGT_PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE lemus_qvgtcore lemus_qvgt_fonts lemus_qvgt_panel)
endfunction()

# The board headless: boot, the LCD and the latches as they change, keys, a render at the DSP's rate.
add_executable(qvgtrun ${QVGT_DIR}/tools/qvgtrun/main.cpp)
target_link_libraries(qvgtrun PRIVATE lemus_qvgtcore)

# A netlist through the solver, and as an ngspice deck (tools/spice/compare_circuit.py).
add_executable(qvgtcircuitrun ${QVGT_DIR}/tools/circuitrun/main.cpp)
target_link_libraries(qvgtcircuitrun PRIVATE lemus_qvgtcore)
add_executable(qvgtpreamprun ${QVGT_DIR}/tools/preamprun/main.cpp)
target_link_libraries(qvgtpreamprun PRIVATE lemus_qvgtcore)
add_executable(qvgtdspcheck ${QVGT_DIR}/tools/dspcheck/main.cpp)
target_link_libraries(qvgtdspcheck PRIVATE lemus_qvgtcore)
add_executable(qvgtbench ${QVGT_DIR}/tools/bench/main.cpp)
target_link_libraries(qvgtbench PRIVATE lemus_qvgtcore)
add_executable(qvgtrender ${QVGT_DIR}/tools/render/main.cpp)
target_link_libraries(qvgtrender PRIVATE lemus_qvgtcore)

# Headless test of the machine's processor. Needs the ROM from the private folder.
juce_add_console_app(qvgtplugintest PRODUCT_NAME "qvgtplugintest")
target_sources(qvgtplugintest PRIVATE ${QVGT_DIR}/tools/plugintest/main.cpp ${QVGT_PLUGIN_SOURCES} ${LEMUS_ROOT_DIR}/host/Roms.cpp)
target_include_directories(qvgtplugintest PRIVATE ${QVGT_PLUGIN_INCLUDES})
target_compile_definitions(qvgtplugintest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 QVGT_ROM="${QVGT_ROM}" QVGT_LCD_ROM="${QVGT_LCD_ROM}")
target_link_libraries(qvgtplugintest PRIVATE lemus_qvgtcore lemus_qvgt_fonts lemus_qvgt_panel
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)

# The extremes: unconverged solves and output out of bounds, the preamp's settings and the unit's
# programs under hard input (tools/corners).
juce_add_console_app(qvgtcorners PRODUCT_NAME "qvgtcorners")
target_sources(qvgtcorners PRIVATE ${QVGT_DIR}/tools/corners/main.cpp ${QVGT_PLUGIN_SOURCES} ${LEMUS_ROOT_DIR}/host/Roms.cpp)
target_include_directories(qvgtcorners PRIVATE ${QVGT_PLUGIN_INCLUDES})
target_compile_definitions(qvgtcorners PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 QVGT_ROM="${QVGT_ROM}" QVGT_LCD_ROM="${QVGT_LCD_ROM}")
target_link_libraries(qvgtcorners PRIVATE lemus_qvgtcore lemus_qvgt_fonts lemus_qvgt_panel
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)
