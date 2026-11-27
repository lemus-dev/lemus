# ProCo RAT (1984 White Face): the circuit model (core/, no JUCE), the driver and editor (rat.cpp, plugin/),
# and the development tools.

set(RAT_DIR ${CMAKE_CURRENT_LIST_DIR})

add_library(lemus_ratcore STATIC
  ${RAT_DIR}/core/dk.cpp
  ${RAT_DIR}/core/pedal.cpp
  ${RAT_DIR}/core/unit.cpp
  ${RAT_DIR}/core/oversampler.cpp
)
target_include_directories(lemus_ratcore PUBLIC ${LEMUS_MACHINES_DIR})
# MSVC and clang-cl take their own flags (clang-cl ignores the GCC ones and reads -Wall as every
# warning there is).
if(MSVC)
  target_compile_options(lemus_ratcore PRIVATE /O2 /W3)
else()
  target_compile_options(lemus_ratcore PRIVATE -O3 -fno-math-errno -ffp-contract=fast -Wall -Wextra)
endif()
set(RAT_TOOL_FLAGS $<IF:$<BOOL:${MSVC}>,/O2;/W3,-O3;-Wall;-Wextra>)
set_target_properties(lemus_ratcore PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The editor loads its fonts from BinaryData (header <BinaryData.h>): the bar's IBM Plex Mono and
# the legends' Nimbus Sans Bold (fonts/README.md).
juce_add_binary_data(lemus_rat_fonts SOURCES
  ${RAT_DIR}/fonts/NimbusSans-Bold.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-Medium.ttf
  ${LEMUS_FONTS_DIR}/IBMPlexMono-SemiBold.ttf
)

set(RAT_PLUGIN_SOURCES
  ${RAT_DIR}/plugin/PluginProcessor.cpp
  ${RAT_DIR}/plugin/PluginEditor.cpp
  ${RAT_DIR}/plugin/HardwareView.cpp
  ${RAT_DIR}/plugin/Fonts.cpp
)
# The editor includes host/Artwork.h and host/PanelFrame.h (the brand marks come from the user's
# artwork at run time; the panel data has only their places).
set(RAT_PLUGIN_INCLUDES ${RAT_DIR}/plugin ${LEMUS_ROOT_DIR})
set_source_files_properties(${RAT_PLUGIN_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${RAT_PLUGIN_INCLUDES}")

lemus_machine(rat fx)
lemus_plugin(rat NAME "pr-dist-r" CODE Prdi VERSION 0.1.1 STAGE beta
  DESCRIPTION "Emulation of the ProCo RAT distortion pedal (1984). Not affiliated with Pro Co Sound.")
function(lemus_add_rat target)
  target_sources(${target} PRIVATE ${RAT_DIR}/rat.cpp ${RAT_PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE lemus_ratcore lemus_rat_fonts)
endfunction()

# Tools. Command-line renderer: WAV through the model, DC point, small-signal response, benchmark.
add_executable(ratrender ${RAT_DIR}/tools/ratrender/ratrender.cpp)
target_compile_options(ratrender PRIVATE ${RAT_TOOL_FLAGS})
target_link_libraries(ratrender PRIVATE lemus_ratcore)

# Golden renders and a benchmark, for optimising without changing the sound.
add_executable(ratgolden ${RAT_DIR}/tools/ratgolden/ratgolden.cpp)
target_compile_options(ratgolden PRIVATE ${RAT_TOOL_FLAGS})
target_link_libraries(ratgolden PRIVATE lemus_ratcore)

# The extremes of the controls and the input: unconverged solves, output out of bounds (tools/corners).
add_executable(ratcorners ${RAT_DIR}/tools/corners/main.cpp)
target_compile_options(ratcorners PRIVATE ${RAT_TOOL_FLAGS})
target_link_libraries(ratcorners PRIVATE lemus_ratcore)

# Headless test of the machine's processor: parameters, bypass, state, stress, long runs, rates.
juce_add_console_app(ratplugintest PRODUCT_NAME "ratplugintest")
target_sources(ratplugintest PRIVATE ${RAT_DIR}/tools/plugintest/main.cpp ${RAT_PLUGIN_SOURCES})
target_include_directories(ratplugintest PRIVATE ${RAT_PLUGIN_INCLUDES})
target_compile_definitions(ratplugintest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="ProCo RAT")
target_link_libraries(ratplugintest PRIVATE lemus_ratcore lemus_rat_fonts
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
