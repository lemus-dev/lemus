# Roland TR-909: the CPU board and firmware host (the original v4.0 firmware on a uPD7811 core),
# the voicing board (every analog circuit run from its netlist, core/netlists/; the hi-hat and
# cymbals from the user's sample ROMs through their counters, DACs and VCAs), the driver and editor
# (tr909.cpp, plugin/), and the development tools.

set(TR909_DIR ${CMAKE_CURRENT_LIST_DIR})
set(TR909_ROM_DIR ${LEMUS_PRIVATE_DIR}/roms/tr909)

# The netlists, as string literals in generated/tr909/netlists.h.
file(GLOB TR909_NETLISTS CONFIGURE_DEPENDS ${TR909_DIR}/core/netlists/*.cir)
set(TR909_NETLIST_NAMES "")
foreach(f ${TR909_NETLISTS})
  get_filename_component(n ${f} NAME_WE)
  list(APPEND TR909_NETLIST_NAMES ${n})
endforeach()
set(TR909_GENERATED ${CMAKE_BINARY_DIR}/generated/tr909/netlists.h)
# commas, not "\;": the Visual Studio generator keeps the backslash in a custom command's argument
string(REPLACE ";" "," TR909_NAMES_ARG "${TR909_NETLIST_NAMES}")
add_custom_command(OUTPUT ${TR909_GENERATED}
  COMMAND ${CMAKE_COMMAND} -DDIR=${TR909_DIR}/core/netlists -DOUT=${TR909_GENERATED} "-DNAMES=${TR909_NAMES_ARG}"
          -P ${TR909_DIR}/tools/cmake/netlists.cmake
  DEPENDS ${TR909_NETLISTS} ${TR909_DIR}/tools/cmake/netlists.cmake
  COMMENT "TR-909 netlists")

# The uPD7810/7811 core, ported from MAME (BSD-3-Clause) by tools/port_upd7810.py.
add_library(lemus_upd7810 STATIC
  ${TR909_DIR}/core/upd7810/upd7810.cpp
  ${TR909_DIR}/core/upd7810/upd7810_opcodes.cpp
  ${TR909_DIR}/core/upd7810/upd7810_table.cpp
)
target_include_directories(lemus_upd7810 PUBLIC ${LEMUS_MACHINES_DIR})
if(MSVC)
  target_compile_options(lemus_upd7810 PRIVATE /O2 /W0)
else()
  target_compile_options(lemus_upd7810 PRIVATE -O3 -w)
endif()
set_target_properties(lemus_upd7810 PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(lemus_tr909core STATIC
  ${TR909_DIR}/core/circuit.cpp
  ${TR909_DIR}/core/oversampler.cpp
  ${TR909_DIR}/core/machine.cpp
  ${TR909_DIR}/core/factory.cpp
  ${TR909_DIR}/core/voiceboard.cpp
  ${TR909_DIR}/core/noise.cpp
  ${TR909_DIR}/core/cymbals.cpp
  ${TR909_DIR}/core/hihat.cpp
  ${TR909_DIR}/core/parallel.cpp
  ${TR909_GENERATED}
)
target_include_directories(lemus_tr909core PUBLIC ${LEMUS_MACHINES_DIR} ${CMAKE_BINARY_DIR}/generated)
find_package(Threads REQUIRED) # core/cymbals.cpp times the clocks on a few threads
target_link_libraries(lemus_tr909core PUBLIC lemus_upd7810 Threads::Threads)
if(MSVC)
  target_compile_options(lemus_tr909core PRIVATE /O2 /W3)
else()
  target_compile_options(lemus_tr909core PRIVATE -O3 -Wall -Wextra)
  # The solver's per-sample copies are a few doubles: kept inline rather than made memcpy calls.
  set_source_files_properties(${TR909_DIR}/core/circuit.cpp PROPERTIES COMPILE_OPTIONS "-fno-builtin-memcpy;-fno-builtin-memmove")
endif()
set_target_properties(lemus_tr909core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Tools. The firmware test driver: script the panel and MIDI, log triggers, DACs, LEDs.
add_executable(tr909seq ${TR909_DIR}/tools/tr909seq/tr909seq.cpp)
target_link_libraries(tr909seq PRIVATE lemus_tr909core)
target_compile_definitions(tr909seq PRIVATE TR909_ROM_PATH="${TR909_ROM_DIR}/tr909_v4.ic609")

# The factory memory (core/factory.h) from the user's v1.0 firmware, checked on v4.0.
add_executable(tr909factory ${TR909_DIR}/tools/factory/main.cpp)
target_link_libraries(tr909factory PRIVATE lemus_tr909core)

# MAME's uPD7810 disassembler (BSD-3-Clause) as a command-line tool.
add_executable(dis7810 ${TR909_DIR}/tools/dis7810/dis7810.cpp ${TR909_DIR}/tools/dis7810/upd7810_dasm.cpp)
target_include_directories(dis7810 PRIVATE ${TR909_DIR}/tools/dis7810)

# The voicing board alone: hits to WAV, knobs, part values, every jack, board signals.
add_executable(tr909voicerun ${TR909_DIR}/tools/voicerun/main.cpp)
target_link_libraries(tr909voicerun PRIVATE lemus_tr909core)
target_compile_definitions(tr909voicerun PRIVATE TR909_ROM_DIR="${TR909_ROM_DIR}")

# The hi-hat's digital half and netlist alone: hits to WAV, ngspice decks with the D/A's exact
# times (docs/circuit/hh.md).
add_executable(tr909hhrun ${TR909_DIR}/tools/hhrun/main.cpp)
target_link_libraries(tr909hhrun PRIVATE lemus_tr909core)
target_compile_definitions(tr909hhrun PRIVATE TR909_ROM_PATH="${TR909_ROM_DIR}/hn61256p__c43.ic69"
  TR909_NETLIST_DIR="${TR909_DIR}/core/netlists")

# The crash's or ride's digital half and netlist alone: a hit to WAV (docs/circuit/cymbals.md).
add_executable(tr909cyrun ${TR909_DIR}/tools/cyrun/main.cpp)
target_link_libraries(tr909cyrun PRIVATE lemus_tr909core)
target_compile_definitions(tr909cyrun PRIVATE TR909_ROM_DIR="${TR909_ROM_DIR}")

# One netlist through the circuit solver, and its ngspice deck (tools/spice).
add_executable(tr909circuitrun ${TR909_DIR}/tools/circuitrun/main.cpp)
target_link_libraries(tr909circuitrun PRIVATE lemus_tr909core)

# One partition stepped alone on the inputs a run recorded (TR909_RECORD): the solver's speed.
add_executable(tr909replay ${TR909_DIR}/tools/replay/main.cpp)
target_link_libraries(tr909replay PRIVATE lemus_tr909core)

juce_add_binary_data(lemus_tr909_fonts HEADER_NAME TR909Fonts.h NAMESPACE TR909Fonts SOURCES
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Medium.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-SemiBold.ttf
)

# The panel's measured layout, read by the editor as it is (docs/panel.json).
juce_add_binary_data(lemus_tr909_panel HEADER_NAME TR909Panel.h NAMESPACE TR909Panel SOURCES ${TR909_DIR}/docs/panel.json)
# The boards as the service notes draw them (docs/boards.json), for SERVICE.
juce_add_binary_data(lemus_tr909_boards HEADER_NAME TR909Boards.h NAMESPACE TR909Boards SOURCES ${TR909_DIR}/docs/boards.json)

# The processor and editor. The editor includes host/Artwork.h (the brand marks come from the
# artwork pack at run time).
file(GLOB TR909_PLUGIN_SOURCES CONFIGURE_DEPENDS ${TR909_DIR}/plugin/*.cpp)
set(TR909_PLUGIN_INCLUDES ${TR909_DIR}/plugin ${LEMUS_ROOT_DIR} ${LEMUS_MACHINES_DIR})
set_source_files_properties(${TR909_PLUGIN_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${TR909_PLUGIN_INCLUDES}")

lemus_machine(tr909 instrument)
lemus_plugin(tr909 NAME "ro-drums-909" CODE R909 VERSION 0.2.2 STAGE beta STANDALONE MIDI_OUT
  DESCRIPTION "Emulation of the Roland TR-909. Not affiliated with Roland.")
function(lemus_add_tr909 target)
  target_sources(${target} PRIVATE ${TR909_DIR}/tr909.cpp ${TR909_PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE lemus_tr909core lemus_tr909_fonts lemus_tr909_panel lemus_tr909_boards)
endfunction()

# Headless test of the machine's processor: MIDI through the firmware and in voice mode, accent,
# sync, sleep, state; --panel renders the editor. Reads the ROMs from the private folder.
juce_add_console_app(tr909plugintest PRODUCT_NAME "tr909plugintest")
target_sources(tr909plugintest PRIVATE ${TR909_DIR}/tools/plugintest/main.cpp ${TR909_PLUGIN_SOURCES})
target_include_directories(tr909plugintest PRIVATE ${TR909_PLUGIN_INCLUDES})
target_compile_definitions(tr909plugintest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 TR909_ROM_DIR="${TR909_ROM_DIR}")
target_link_libraries(tr909plugintest PRIVATE lemus_tr909core lemus_tr909_fonts lemus_tr909_panel lemus_tr909_boards
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)

# CPU benchmark and golden-reference check (tools/bench): the processor's workloads timed at 44.1
# and 48 kHz, written as golden WAVs or compared with them.
juce_add_console_app(tr909bench PRODUCT_NAME "tr909bench")
target_sources(tr909bench PRIVATE ${TR909_DIR}/tools/bench/main.cpp ${TR909_PLUGIN_SOURCES})
target_include_directories(tr909bench PRIVATE ${TR909_PLUGIN_INCLUDES})
target_compile_definitions(tr909bench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 TR909_ROM_DIR="${TR909_ROM_DIR}")
target_link_libraries(tr909bench PRIVATE lemus_tr909core lemus_tr909_fonts lemus_tr909_panel lemus_tr909_boards
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
