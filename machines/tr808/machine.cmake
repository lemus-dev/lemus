# Roland TR-808: the CPU board and firmware host, the voicing board (every voice's circuit run from
# its netlist, core/netlists/), the driver and editor (tr808.cpp, plugin/), and the development
# tools.

set(TR808_DIR ${CMAKE_CURRENT_LIST_DIR})
set(TR808_ROM_PATH ${LEMUS_PRIVATE_DIR}/roms/tr808/d650c-085.ic4)

# The netlists, as string literals in generated/tr808/netlists.h.
file(GLOB TR808_NETLISTS CONFIGURE_DEPENDS ${TR808_DIR}/core/netlists/*.cir)
set(TR808_NETLIST_NAMES "")
foreach(f ${TR808_NETLISTS})
  get_filename_component(n ${f} NAME_WE)
  list(APPEND TR808_NETLIST_NAMES ${n})
endforeach()
set(TR808_GENERATED ${CMAKE_BINARY_DIR}/generated/tr808/netlists.h)
# commas, not "\;": the Visual Studio generator keeps the backslash in a custom command's argument
string(REPLACE ";" "," TR808_NAMES_ARG "${TR808_NETLIST_NAMES}")
add_custom_command(OUTPUT ${TR808_GENERATED}
  COMMAND ${CMAKE_COMMAND} -DDIR=${TR808_DIR}/core/netlists -DOUT=${TR808_GENERATED} "-DNAMES=${TR808_NAMES_ARG}"
          -P ${TR808_DIR}/tools/cmake/netlists.cmake
  DEPENDS ${TR808_NETLISTS} ${TR808_DIR}/tools/cmake/netlists.cmake
  COMMENT "TR-808 netlists")

add_library(lemus_tr808core STATIC
  ${TR808_DIR}/core/circuit.cpp
  ${TR808_DIR}/core/voiceboard.cpp
  ${TR808_DIR}/core/parallel.cpp
  ${TR808_DIR}/core/oversampler.cpp
  ${TR808_DIR}/core/ucom43.cpp
  ${TR808_DIR}/core/machine.cpp
  ${TR808_DIR}/core/factory.cpp
  ${TR808_DIR}/core/trimmers.cpp
  ${TR808_GENERATED}
)
target_include_directories(lemus_tr808core PUBLIC ${LEMUS_MACHINES_DIR} ${CMAKE_BINARY_DIR}/generated)
find_package(Threads REQUIRED) # core/parallel.cpp: the voices' jobs on worker threads
target_link_libraries(lemus_tr808core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(lemus_tr808core PRIVATE /O2 /W3)
else()
  target_compile_options(lemus_tr808core PRIVATE -O3 -Wall -Wextra)
  # The solver's per-sample copies are a few doubles: kept inline rather than made memcpy calls.
  set_source_files_properties(${TR808_DIR}/core/circuit.cpp PROPERTIES COMPILE_OPTIONS "-fno-builtin-memcpy;-fno-builtin-memmove")
endif()
set_target_properties(lemus_tr808core PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_binary_data(lemus_tr808_fonts HEADER_NAME TR808Fonts.h NAMESPACE TR808Fonts SOURCES
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Medium.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-SemiBold.ttf
)

# The panel's measured layout, read by the editor as it is (docs/panel.json).
juce_add_binary_data(lemus_tr808_panel HEADER_NAME TR808Panel.h NAMESPACE TR808Panel SOURCES ${TR808_DIR}/docs/panel.json)
# The boards as the service notes draw them (docs/boards.json), for SERVICE.
juce_add_binary_data(lemus_tr808_boards HEADER_NAME TR808Boards.h NAMESPACE TR808Boards SOURCES ${TR808_DIR}/docs/boards.json)

set(TR808_PLUGIN_SOURCES
  ${TR808_DIR}/plugin/PluginProcessor.cpp
  ${TR808_DIR}/plugin/PluginEditor.cpp
  ${TR808_DIR}/plugin/HardwareView.cpp
  ${TR808_DIR}/plugin/HardwareService.cpp
  ${TR808_DIR}/plugin/Fonts.cpp
)
# The editor includes host/Artwork.h (the brand marks come from the artwork pack at run time).
set(TR808_PLUGIN_INCLUDES ${TR808_DIR}/plugin ${LEMUS_ROOT_DIR} ${LEMUS_MACHINES_DIR})
set_source_files_properties(${TR808_PLUGIN_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${TR808_PLUGIN_INCLUDES}")

lemus_machine(tr808 instrument)
lemus_plugin(tr808 NAME "ro-drums-808" CODE R808 VERSION 0.1.0 STAGE beta STANDALONE
  DESCRIPTION "Emulation of the Roland TR-808. Not affiliated with Roland.")
function(lemus_add_tr808 target)
  target_sources(${target} PRIVATE ${TR808_DIR}/tr808.cpp ${TR808_PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE lemus_tr808core lemus_tr808_fonts lemus_tr808_panel lemus_tr808_boards)
endfunction()

# Tools. The firmware test driver: script the panel, log the step pulses and their instrument data.
add_executable(tr808seq ${TR808_DIR}/tools/tr808seq/tr808seq.cpp)
target_link_libraries(tr808seq PRIVATE lemus_tr808core)

# One netlist through the circuit solver, and its ngspice deck (tools/spice).
add_executable(circuitrun ${TR808_DIR}/tools/circuitrun/main.cpp)
target_link_libraries(circuitrun PRIVATE lemus_tr808core)

# The voicing board alone: hits to WAV, per-voice CPU and solver statistics.
add_executable(voicerun ${TR808_DIR}/tools/voicerun/main.cpp)
target_link_libraries(voicerun PRIVATE lemus_tr808core)

# Headless test of the machine's processor: host transport (DIN sync), MIDI, internal clock,
# sleep, state. Needs the ROM from the private folder.
juce_add_console_app(tr808plugintest PRODUCT_NAME "tr808plugintest")
target_sources(tr808plugintest PRIVATE ${TR808_DIR}/tools/plugintest/main.cpp ${TR808_PLUGIN_SOURCES})
target_include_directories(tr808plugintest PRIVATE ${TR808_PLUGIN_INCLUDES})
target_compile_definitions(tr808plugintest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  TR808_ROM_PATH="${TR808_ROM_PATH}")
target_link_libraries(tr808plugintest PRIVATE lemus_tr808core lemus_tr808_fonts lemus_tr808_panel lemus_tr808_boards
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)

# CPU benchmark and golden-reference check (tools/bench): workloads timed at 44.1 and 48 kHz and
# compared with golden WAVs, so speed work can show it leaves the sound alone.
juce_add_console_app(tr808bench PRODUCT_NAME "tr808bench")
target_sources(tr808bench PRIVATE ${TR808_DIR}/tools/bench/main.cpp ${TR808_PLUGIN_SOURCES})
target_include_directories(tr808bench PRIVATE ${TR808_PLUGIN_INCLUDES})
target_compile_definitions(tr808bench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  TR808_ROM_PATH="${TR808_ROM_PATH}")
target_link_libraries(tr808bench PRIVATE lemus_tr808core lemus_tr808_fonts lemus_tr808_panel lemus_tr808_boards
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
