# Roland TB-303: the CPU board, firmware host and voice circuit (core/, no JUCE), the driver and
# editor (tb303.cpp, plugin/), and the development tools.

set(TB303_DIR ${CMAKE_CURRENT_LIST_DIR})

add_library(lemus_tb303core STATIC
  ${TB303_DIR}/core/voice.cpp
  ${TB303_DIR}/core/oversampler.cpp
  ${TB303_DIR}/core/ucom43.cpp
  ${TB303_DIR}/core/machine.cpp
)
target_include_directories(lemus_tb303core PUBLIC ${LEMUS_MACHINES_DIR})
if(MSVC)
  target_compile_options(lemus_tb303core PRIVATE /O2 /W3)
else()
  target_compile_options(lemus_tb303core PRIVATE -O3 -Wall -Wextra)
endif()
set_target_properties(lemus_tb303core PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_binary_data(lemus_tb303_fonts HEADER_NAME TB303Fonts.h NAMESPACE TB303Fonts SOURCES
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Medium.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-SemiBold.ttf
)

set(TB303_PLUGIN_SOURCES
  ${TB303_DIR}/plugin/PluginProcessor.cpp
  ${TB303_DIR}/plugin/PluginEditor.cpp
  ${TB303_DIR}/plugin/HardwareView.cpp
  ${TB303_DIR}/plugin/HardwareService.cpp
  ${TB303_DIR}/plugin/Fonts.cpp
)
# The editor includes host/Artwork.h (the brand marks come from the artwork pack at run time).
set(TB303_PLUGIN_INCLUDES ${TB303_DIR}/plugin ${LEMUS_ROOT_DIR})
set_source_files_properties(${TB303_PLUGIN_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${TB303_PLUGIN_INCLUDES}")

lemus_machine(tb303 instrument)
lemus_plugin(tb303 NAME "ro-mono-303" CODE R303 STANDALONE
  DESCRIPTION "Emulation of the Roland TB-303. Not affiliated with Roland.")
function(lemus_add_tb303 target)
  target_sources(${target} PRIVATE ${TB303_DIR}/tb303.cpp ${TB303_PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE lemus_tb303core lemus_tb303_fonts)
endfunction()

# Tools. The firmware test driver: script the panel, log CV/gate/accent/slide and LEDs.
add_executable(tb303seq ${TB303_DIR}/tools/tb303seq/tb303seq.cpp)
target_link_libraries(tb303seq PRIVATE lemus_tb303core)

# Renderer for the comparison tests (tools/compare): test specs to WAV, and measurements.
juce_add_console_app(tb303render PRODUCT_NAME "tb303render")
target_sources(tb303render PRIVATE ${TB303_DIR}/tools/tb303render/tb303render.cpp)
target_compile_definitions(tb303render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(tb303render PRIVATE lemus_tb303core juce::juce_audio_formats juce::juce_recommended_config_flags)

# Headless test of the machine's processor: host transport (DIN sync), MIDI, internal clock,
# state. Needs the ROM from the private folder.
juce_add_console_app(tb303plugintest PRODUCT_NAME "tb303plugintest")
target_sources(tb303plugintest PRIVATE ${TB303_DIR}/tools/plugintest/main.cpp ${TB303_PLUGIN_SOURCES})
target_include_directories(tb303plugintest PRIVATE ${TB303_PLUGIN_INCLUDES})
target_compile_definitions(tb303plugintest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  TB303_ROM_PATH="${LEMUS_PRIVATE_DIR}/roms/tb303/d650c-133.ic8")
target_link_libraries(tb303plugintest PRIVATE lemus_tb303core lemus_tb303_fonts
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)

# One block of the voice model on its own, for the SPICE comparisons (tools/spice). It builds
# core/voice.cpp into itself, so it doesn't link the core library.
add_executable(tb303blocks ${TB303_DIR}/tools/spice/blocks.cpp ${TB303_DIR}/core/oversampler.cpp)
target_include_directories(tb303blocks PRIVATE ${LEMUS_MACHINES_DIR})
target_compile_options(tb303blocks PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2>)

# CPU benchmark and golden-reference check (tools/bench): workloads timed at 44.1 and 48 kHz and
# compared with golden WAVs, so speed work can show it leaves the sound alone.
juce_add_console_app(tb303bench PRODUCT_NAME "tb303bench")
target_sources(tb303bench PRIVATE ${TB303_DIR}/tools/bench/main.cpp ${TB303_PLUGIN_SOURCES})
target_include_directories(tb303bench PRIVATE ${TB303_PLUGIN_INCLUDES})
target_compile_definitions(tb303bench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  TB303_ROM_PATH="${LEMUS_PRIVATE_DIR}/roms/tb303/d650c-133.ic8")
target_link_libraries(tb303bench PRIVATE lemus_tb303core lemus_tb303_fonts
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
