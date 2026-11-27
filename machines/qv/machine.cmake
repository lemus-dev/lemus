# Alesis QuadraVerb and QuadraVerb Plus: the firmware on an 80C31 (MAME's mcs51 core) with the DASP 24
# effects DSP as decoded from it, and the board's analog stages (core/, no JUCE; copied from
# machines/qvgt), the driver and editor (qv.cpp, plugin/), and the development tools.

set(QV_DIR ${CMAKE_CURRENT_LIST_DIR})
set(QV_ROM ${LEMUS_PRIVATE_DIR}/roms/qv/qvv203.bin)
set(QV_ROM_V107 ${LEMUS_PRIVATE_DIR}/roms/qv/qvv107.bin)
set(QV_LCD_ROM ${LEMUS_PRIVATE_DIR}/roms/qv/hd44780_a00.bin)
include(${QV_DIR}/tools/cmake/core.cmake)

juce_add_binary_data(lemus_qv_fonts HEADER_NAME QVFonts.h NAMESPACE QVFonts SOURCES
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Medium.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-SemiBold.ttf
)

juce_add_binary_data(lemus_qv_panel HEADER_NAME QVPanel.h NAMESPACE QVPanel SOURCES ${QV_DIR}/docs/panel.json)

set(QV_PLUGIN_SOURCES
  ${QV_DIR}/plugin/PluginProcessor.cpp
  ${QV_DIR}/plugin/PluginEditor.cpp
  ${QV_DIR}/plugin/HardwareView.cpp
  ${QV_DIR}/plugin/Fonts.cpp
)
set(QV_PLUGIN_INCLUDES ${QV_DIR}/plugin ${LEMUS_ROOT_DIR} ${LEMUS_MACHINES_DIR})
set_source_files_properties(${QV_PLUGIN_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${QV_PLUGIN_INCLUDES}")

lemus_machine(qv fx)
lemus_plugin(qv NAME "al-fx-quad" CODE Aqua VERSION 0.1.0 STAGE beta STANDALONE MIDI_OUT
  DESCRIPTION "Emulation of the Alesis QuadraVerb and QuadraVerb Plus. Not affiliated with Alesis.")
function(lemus_add_qv target)
  target_sources(${target} PRIVATE ${QV_DIR}/qv.cpp ${QV_PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE lemus_qvcore lemus_qv_fonts lemus_qv_panel)
endfunction()

# The board headless: boot, the LCD and the latches as they change, keys, a render at the DSP's rate.
add_executable(qvrun ${QV_DIR}/tools/qvrun/main.cpp)
target_link_libraries(qvrun PRIVATE lemus_qvcore)

# The linear analog stages' responses as the model runs them (tools/spice/ac.py compares them with
# ngspice).
add_executable(qvanalog ${QV_DIR}/tools/analog/main.cpp)
target_link_libraries(qvanalog PRIVATE lemus_qvcore)
# The whole unit's frequency response, wet and dry, on flat programs.
add_executable(qvresponse ${QV_DIR}/tools/response/main.cpp)
target_link_libraries(qvresponse PRIVATE lemus_qvcore)
# A recording's dry source through the unit, whole and as its dry and wet paths (tools/compare).
add_executable(qvcompare ${QV_DIR}/tools/compare/main.cpp)
target_link_libraries(qvcompare PRIVATE lemus_qvcore)
add_executable(qvdspcheck ${QV_DIR}/tools/dspcheck/main.cpp)
target_link_libraries(qvdspcheck PRIVATE lemus_qvcore)
add_executable(qvbench ${QV_DIR}/tools/bench/main.cpp)
target_link_libraries(qvbench PRIVATE lemus_qvcore)
add_executable(qvrender ${QV_DIR}/tools/render/main.cpp)
target_link_libraries(qvrender PRIVATE lemus_qvcore)

# Headless test of the machine's processor. Needs the ROM from the private folder.
juce_add_console_app(qvplugintest PRODUCT_NAME "qvplugintest")
target_sources(qvplugintest PRIVATE ${QV_DIR}/tools/plugintest/main.cpp ${QV_PLUGIN_SOURCES} ${LEMUS_ROOT_DIR}/host/Roms.cpp)
target_include_directories(qvplugintest PRIVATE ${QV_PLUGIN_INCLUDES})
target_compile_definitions(qvplugintest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 QV_ROM="${QV_ROM}" QV_ROM_V107="${QV_ROM_V107}" QV_LCD_ROM="${QV_LCD_ROM}")
target_link_libraries(qvplugintest PRIVATE lemus_qvcore lemus_qv_fonts lemus_qv_panel
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)

# The extremes: unconverged solves and output out of bounds, the unit's programs under hard input
# (tools/corners).
juce_add_console_app(qvcorners PRODUCT_NAME "qvcorners")
target_sources(qvcorners PRIVATE ${QV_DIR}/tools/corners/main.cpp ${QV_PLUGIN_SOURCES} ${LEMUS_ROOT_DIR}/host/Roms.cpp)
target_include_directories(qvcorners PRIVATE ${QV_PLUGIN_INCLUDES})
target_compile_definitions(qvcorners PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 QV_ROM="${QV_ROM}" QV_ROM_V107="${QV_ROM_V107}" QV_LCD_ROM="${QV_LCD_ROM}")
target_link_libraries(qvcorners PRIVATE lemus_qvcore lemus_qv_fonts lemus_qv_panel
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)
