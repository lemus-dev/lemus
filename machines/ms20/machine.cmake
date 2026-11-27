# Korg MS-20 (Rev 1, two KORG35 filters): the circuit model (core/, no JUCE; the blocks run from
# their netlists, core/netlists/, transcribed from the service manual), the processor and editor
# (plugin/), the driver (ms20.cpp) and the development tools. No CPU, no ROM.

set(MS20_DIR ${CMAKE_CURRENT_LIST_DIR})

# The netlists, as string literals in generated/ms20/netlists.h.
file(GLOB MS20_NETLISTS CONFIGURE_DEPENDS ${MS20_DIR}/core/netlists/*.cir)
set(MS20_NETLIST_NAMES "")
foreach(f ${MS20_NETLISTS})
  get_filename_component(n ${f} NAME_WE)
  list(APPEND MS20_NETLIST_NAMES ${n})
endforeach()
set(MS20_GENERATED ${CMAKE_BINARY_DIR}/generated/ms20/netlists.h)
# commas, not escaped semicolons: Visual Studio splits a "\;" list into separate arguments
string(REPLACE ";" "," MS20_NAMES_ARG "${MS20_NETLIST_NAMES}")
add_custom_command(OUTPUT ${MS20_GENERATED}
  COMMAND ${CMAKE_COMMAND} -DDIR=${MS20_DIR}/core/netlists -DOUT=${MS20_GENERATED} "-DNAMES=${MS20_NAMES_ARG}"
          -P ${MS20_DIR}/tools/cmake/netlists.cmake
  DEPENDS ${MS20_NETLISTS} ${MS20_DIR}/tools/cmake/netlists.cmake
  COMMENT "MS-20 netlists")

add_library(lemus_ms20core STATIC
  ${MS20_DIR}/core/circuit.cpp
  ${MS20_DIR}/core/oversampler.cpp
  ${MS20_DIR}/core/board.cpp
  ${MS20_DIR}/core/vco.cpp
  ${MS20_DIR}/core/machine.cpp
  ${MS20_DIR}/core/worker.cpp
  ${MS20_GENERATED}
)
target_include_directories(lemus_ms20core PUBLIC ${LEMUS_MACHINES_DIR} ${CMAKE_BINARY_DIR}/generated)
target_compile_features(lemus_ms20core PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(lemus_ms20core PRIVATE /O2 /W3)
else()
  target_compile_options(lemus_ms20core PRIVATE -O3 -Wall -Wextra)
  # The solver's per-sample copies are a few doubles: kept inline rather than made memcpy calls.
  set_source_files_properties(${MS20_DIR}/core/circuit.cpp PROPERTIES COMPILE_OPTIONS "-fno-builtin-memcpy;-fno-builtin-memmove")
endif()
set_target_properties(lemus_ms20core PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED) # core/worker.cpp: the control side on a thread of its own
target_link_libraries(lemus_ms20core PUBLIC Threads::Threads)

# Fonts for the bar, the key list and tooltips.
juce_add_binary_data(lemus_ms20_fonts HEADER_NAME Ms20Fonts.h NAMESPACE Ms20Fonts SOURCES
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Medium.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-SemiBold.ttf
  ${MS20_DIR}/fonts/NimbusSansNarrow-Regular.ttf
)

# The panel's measured layout, read by the editor as it is (docs/panel.json).
juce_add_binary_data(lemus_ms20_panel HEADER_NAME Ms20Panel.h NAMESPACE Ms20Panel SOURCES ${MS20_DIR}/docs/panel.json)

# The processor and editor (plugin/).
set(MS20_PLUGIN_SOURCES
  ${MS20_DIR}/plugin/PluginProcessor.cpp
  ${MS20_DIR}/plugin/PluginEditor.cpp
  ${MS20_DIR}/plugin/HardwareView.cpp
  ${MS20_DIR}/plugin/Fonts.cpp
)
set(MS20_PLUGIN_INCLUDES ${MS20_DIR}/plugin ${LEMUS_ROOT_DIR} ${LEMUS_MACHINES_DIR})
set_source_files_properties(${MS20_PLUGIN_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${MS20_PLUGIN_INCLUDES}")

lemus_machine(ms20 instrument)
lemus_plugin(ms20 NAME "ko-mono-20" CODE K20m VERSION 0.1.0 STAGE beta STANDALONE
  DESCRIPTION "Emulation of the Korg MS-20. Not affiliated with Korg.")
function(lemus_add_ms20 target)
  target_sources(${target} PRIVATE ${MS20_DIR}/ms20.cpp ${MS20_PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE lemus_ms20core lemus_ms20_fonts lemus_ms20_panel)
endfunction()

# Headless test of the machine's processor (tools/plugintest; --panel opens the editor too).
juce_add_console_app(ms20plugintest PRODUCT_NAME "ms20plugintest")
target_sources(ms20plugintest PRIVATE ${MS20_DIR}/tools/plugintest/main.cpp ${MS20_PLUGIN_SOURCES})
target_include_directories(ms20plugintest PRIVATE ${MS20_PLUGIN_INCLUDES})
target_compile_definitions(ms20plugintest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ms20plugintest PRIVATE lemus_ms20core lemus_ms20_fonts lemus_ms20_panel
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)

# The controls' extremes: unconverged solves, noise where nothing makes it, bounds (tools/corners).
juce_add_console_app(ms20corners PRODUCT_NAME "ms20corners")
target_sources(ms20corners PRIVATE ${MS20_DIR}/tools/corners/main.cpp ${MS20_PLUGIN_SOURCES})
target_include_directories(ms20corners PRIVATE ${MS20_PLUGIN_INCLUDES})
target_compile_definitions(ms20corners PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ms20corners PRIVATE lemus_ms20core lemus_ms20_fonts lemus_ms20_panel
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)

# One netlist through the circuit solver, and its ngspice deck (tools/spice).
add_executable(ms20circuitrun ${MS20_DIR}/tools/circuitrun/main.cpp)
target_link_libraries(ms20circuitrun PRIVATE lemus_ms20core)

# The machine (core only) playing notes, to WAV, with the board's signals probed (tools/render).
add_executable(ms20render ${MS20_DIR}/tools/render/main.cpp)
target_link_libraries(ms20render PRIVATE lemus_ms20core)

# The service manual's adjustment procedure on the model (tools/service): checks, and the trims.
add_executable(ms20service ${MS20_DIR}/tools/service/main.cpp)
target_link_libraries(ms20service PRIVATE lemus_ms20core)

# The workloads: cost, golden renders, checks against them (tools/bench).
add_executable(ms20bench ${MS20_DIR}/tools/bench/main.cpp)
target_link_libraries(ms20bench PRIVATE lemus_ms20core)
