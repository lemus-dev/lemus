# Novation Bass Station Rack: firmware v1.5 on the MN1880 CPU (MAME's core, core/mn1880/) and the
# analog voice it drives (core/, no JUCE), the driver and editor (bsrack.cpp, plugin/), the tools.

set(BSRACK_DIR ${CMAKE_CURRENT_LIST_DIR})
set(BSRACK_ROM ${LEMUS_PRIVATE_DIR}/roms/bsrack/v1.5.bin)
include(${BSRACK_DIR}/tools/cmake/core.cmake)

# The editor's fonts: the bar's IBM Plex Mono, the legends' Liberation Sans Bold (fonts/, OFL); and
# docs/panel.json, the panel it draws.
juce_add_binary_data(lemus_bsrack_fonts HEADER_NAME BSRackFonts.h NAMESPACE BSRackFonts SOURCES
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Medium.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-SemiBold.ttf
  ${BSRACK_DIR}/fonts/LiberationSans-Bold.ttf
)
juce_add_binary_data(lemus_bsrack_panel HEADER_NAME BSRackPanel.h NAMESPACE BSRackPanel SOURCES ${BSRACK_DIR}/docs/panel.json)

set(BSRACK_PLUGIN_SOURCES
  ${BSRACK_DIR}/plugin/PluginProcessor.cpp
  ${BSRACK_DIR}/plugin/PluginEditor.cpp
  ${BSRACK_DIR}/plugin/HardwareView.cpp
  ${BSRACK_DIR}/plugin/Fonts.cpp
)
set(BSRACK_PLUGIN_INCLUDES ${BSRACK_DIR}/plugin ${LEMUS_ROOT_DIR} ${LEMUS_MACHINES_DIR})
set_source_files_properties(${BSRACK_PLUGIN_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${BSRACK_PLUGIN_INCLUDES}")

lemus_machine(bsrack instrument)
lemus_plugin(bsrack NAME "no-mono-bsr" CODE Nbsr VERSION 0.1.0 STAGE beta STANDALONE MIDI_OUT
  DESCRIPTION "Emulation of the Novation Bass Station Rack. Not affiliated with Novation.")
function(lemus_add_bsrack target)
  target_sources(${target} PRIVATE ${BSRACK_DIR}/bsrack.cpp ${BSRACK_PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE lemus_bsrackcore lemus_bsrack_fonts lemus_bsrack_panel)
endfunction()

# Tools (no JUCE). The board headless: scripted pots, switches, buttons and MIDI in; logs of the
# latches, registers, ADC, CV channels and sync edges out (tools/bsrackseq). The firmware's
# recursive-descent listing (tools/fwdis).
add_executable(bsrackseq ${BSRACK_DIR}/tools/bsrackseq/bsrackseq.cpp)
target_link_libraries(bsrackseq PRIVATE lemus_bsrackcore)
add_executable(bsrackdis ${BSRACK_DIR}/tools/fwdis/main.cpp)
target_link_libraries(bsrackdis PRIVATE lemus_bsrackcore)

# Renders a scripted performance headless to a WAV (tools/render).
add_executable(bsrackrender ${BSRACK_DIR}/tools/render/main.cpp)
target_link_libraries(bsrackrender PRIVATE lemus_bsrackcore)

# Headless test of the machine's processor (tools/plugintest; --panel DIR draws the panel too).
# Needs the ROM from the private folder (BSRACK_ROM).
juce_add_console_app(bsrackplugintest PRODUCT_NAME "bsrackplugintest")
target_sources(bsrackplugintest PRIVATE ${BSRACK_DIR}/tools/plugintest/main.cpp ${BSRACK_PLUGIN_SOURCES} ${LEMUS_ROOT_DIR}/host/Roms.cpp)
target_include_directories(bsrackplugintest PRIVATE ${BSRACK_PLUGIN_INCLUDES})
target_compile_definitions(bsrackplugintest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 BSRACK_ROM="${BSRACK_ROM}")
target_link_libraries(bsrackplugintest PRIVATE lemus_bsrackcore lemus_bsrack_fonts lemus_bsrack_panel
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)

# The workloads: cost, golden renders, checks against them (tools/bench). Needs the ROM.
add_executable(bsrackbench ${BSRACK_DIR}/tools/bench/main.cpp)
target_link_libraries(bsrackbench PRIVATE lemus_bsrackcore)
target_compile_definitions(bsrackbench PRIVATE BSRACK_ROM="${BSRACK_ROM}")

# A netlist through the solver, and as an ngspice deck (tools/spice/suite.py).
add_executable(bsrackcircuitrun ${BSRACK_DIR}/tools/circuitrun/main.cpp ${BSRACK_DIR}/core/circuit.cpp)
target_include_directories(bsrackcircuitrun PRIVATE ${LEMUS_MACHINES_DIR})
target_compile_features(bsrackcircuitrun PRIVATE cxx_std_17)

# The controls' extremes: unconverged solves, non-finite or out-of-bounds output (tools/corners).
add_executable(bsrackcorners ${BSRACK_DIR}/tools/corners/main.cpp)
target_link_libraries(bsrackcorners PRIVATE lemus_bsrackcore)
target_compile_definitions(bsrackcorners PRIVATE BSRACK_ROM="${BSRACK_ROM}")
