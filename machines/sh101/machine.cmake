# Roland SH-101: the CPU board and firmware host, the synth board model (core/, no JUCE), the
# driver and editor (sh101.cpp, plugin/), and the development tools.

set(SH101_DIR ${CMAKE_CURRENT_LIST_DIR})
set(SH101_ROM_PATH ${LEMUS_PRIVATE_DIR}/roms/sh101/tmp80c49p-6-7301.ic6)

# Every .cpp in core/: the voice's sections are split over several files (docs/model.md).
file(GLOB SH101_CORE_SOURCES CONFIGURE_DEPENDS ${SH101_DIR}/core/*.cpp)
add_library(lemus_sh101core STATIC ${SH101_CORE_SOURCES})
target_include_directories(lemus_sh101core PUBLIC ${LEMUS_MACHINES_DIR})
if(MSVC)
  target_compile_options(lemus_sh101core PRIVATE /O2 /W3)
else()
  target_compile_options(lemus_sh101core PRIVATE -O3 -Wall -Wextra)
endif()
set_target_properties(lemus_sh101core PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_binary_data(lemus_sh101_fonts HEADER_NAME SH101Fonts.h NAMESPACE SH101Fonts SOURCES
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Medium.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-SemiBold.ttf
)

set(SH101_PLUGIN_SOURCES
  ${SH101_DIR}/plugin/PluginProcessor.cpp
  ${SH101_DIR}/plugin/PluginEditor.cpp
  ${SH101_DIR}/plugin/HardwareView.cpp
  ${SH101_DIR}/plugin/Fonts.cpp
)
# The editor includes host/Artwork.h (the brand marks come from the artwork pack at run time).
set(SH101_PLUGIN_INCLUDES ${SH101_DIR}/plugin ${LEMUS_ROOT_DIR} ${LEMUS_MACHINES_DIR})
set_source_files_properties(${SH101_PLUGIN_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${SH101_PLUGIN_INCLUDES}")

lemus_machine(sh101 instrument)
lemus_plugin(sh101 NAME "ro-mono-101" CODE R101 VERSION 0.1.5 STAGE beta STANDALONE
  DESCRIPTION "Emulation of the Roland SH-101. Not affiliated with Roland.")
function(lemus_add_sh101 target)
  target_sources(${target} PRIVATE ${SH101_DIR}/sh101.cpp ${SH101_PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE lemus_sh101core lemus_sh101_fonts)
endfunction()

# Tools. The firmware test driver: script the panel, log the DAC holds, P2 and RAM.
add_executable(sh101seq ${SH101_DIR}/tools/sh101seq/sh101seq.cpp ${SH101_DIR}/tools/sh101seq/selftest.cpp)
target_compile_options(sh101seq PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2 -Wall -Wextra>)
target_link_libraries(sh101seq PRIVATE lemus_sh101core)
target_compile_definitions(sh101seq PRIVATE SH101_ROM_PATH="${SH101_ROM_PATH}")

# Console tools that run the machine's processor headless, so they hear exactly what the plugin
# plays: sh101render (test specs to WAV, for tools/compare) and sh101plugintest (sync, MIDI,
# state, stability). They read the ROM from the private folder.
foreach(tool sh101render sh101plugintest)
  if(tool STREQUAL "sh101render")
    set(main ${SH101_DIR}/tools/sh101render/sh101render.cpp)
  else()
    set(main ${SH101_DIR}/tools/plugintest/main.cpp)
  endif()
  juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
  target_sources(${tool} PRIVATE ${main} ${SH101_PLUGIN_SOURCES})
  target_include_directories(${tool} PRIVATE ${SH101_PLUGIN_INCLUDES})
  target_compile_definitions(${tool} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 SH101_ROM_PATH="${SH101_ROM_PATH}")
  target_link_libraries(${tool} PRIVATE lemus_sh101core lemus_sh101_fonts
    juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats
    juce::juce_recommended_config_flags)
endforeach()

# CPU benchmark and golden-reference check (tools/bench): workloads timed at 44.1 and 48 kHz and
# compared with golden WAVs, so speed work can show it leaves the sound alone.
juce_add_console_app(sh101bench PRODUCT_NAME "sh101bench")
target_sources(sh101bench PRIVATE ${SH101_DIR}/tools/bench/main.cpp ${SH101_PLUGIN_SOURCES})
target_include_directories(sh101bench PRIVATE ${SH101_PLUGIN_INCLUDES})
target_compile_definitions(sh101bench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 SH101_ROM_PATH="${SH101_ROM_PATH}")
target_link_libraries(sh101bench PRIVATE lemus_sh101core lemus_sh101_fonts
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)

# The controls' extremes: unconverged solves, noise where nothing makes it (tools/corners).
juce_add_console_app(sh101corners PRODUCT_NAME "sh101corners")
target_sources(sh101corners PRIVATE ${SH101_DIR}/tools/corners/main.cpp ${SH101_PLUGIN_SOURCES})
target_include_directories(sh101corners PRIVATE ${SH101_PLUGIN_INCLUDES})
target_compile_definitions(sh101corners PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 SH101_ROM_PATH="${SH101_ROM_PATH}")
target_link_libraries(sh101corners PRIVATE lemus_sh101core lemus_sh101_fonts
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)

# Tests of the voice against SPICE of the transcribed schematic (tools/spice): each section, and
# the whole voice, builds its own runner from the core library.
foreach(section osc filter voice jack)
  if(EXISTS ${SH101_DIR}/tools/spice/${section}/blocks.cpp)
    add_executable(sh101${section}blocks ${SH101_DIR}/tools/spice/${section}/blocks.cpp)
    target_compile_options(sh101${section}blocks PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2>)
    target_link_libraries(sh101${section}blocks PRIVATE lemus_sh101core)
  endif()
endforeach()
