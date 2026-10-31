# The test host: lemus-render plays scenes (tools/host/README.md) through plugins offline; the
# A/B player plays them for listening. Both are the same player with a different front end, so
# everything they share is set once, in lemus_add_host().

set(LEMUS_HOST_DIR ${CMAKE_CURRENT_LIST_DIR})

# Plugin hosting needs the same definitions in both hosts, and the formats a host can load differ
# per platform: AU exists only on Apple, so only Apple gets JUCE_PLUGINHOST_AU.
set(LEMUS_HOST_DEFINITIONS
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=1
  LEMUS_HOST_DIR="${LEMUS_HOST_DIR}"
  LEMUS_MACHINES_DIR="${LEMUS_MACHINES_DIR}"
)

if(APPLE)
  list(APPEND LEMUS_HOST_DEFINITIONS JUCE_PLUGINHOST_AU=1)
  # AU hosting needs CoreAudioKit (AUGenericView). Only ever pass frameworks to a Darwin linker:
  # gcc/clang on Linux reject -framework, and MSVC rejects it too.
  set(LEMUS_HOST_FRAMEWORKS "-framework CoreAudioKit")
else()
  set(LEMUS_HOST_FRAMEWORKS "")
endif()

# lemus_add_host(<target> EXTRA_LIBS <libs...>)
# The player sources, hosting definitions and the platform-specific link line, shared by both hosts.
# EXTRA_LIBS is the front end's own dependencies.
function(lemus_add_host target)
  cmake_parse_arguments(P "" "" "EXTRA_LIBS" ${ARGN})
  target_sources(${target} PRIVATE
    ${LEMUS_HOST_DIR}/Player.cpp
    ${LEMUS_HOST_DIR}/Scene.cpp
  )
  target_compile_definitions(${target} PRIVATE ${LEMUS_HOST_DEFINITIONS})
  target_link_libraries(${target} PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    ${LEMUS_HOST_FRAMEWORKS}
    ${P_EXTRA_LIBS}
  )
endfunction()

# The render host: no window, so no gui_extra and no juce_audio_utils.
juce_add_console_app(lemus-render PRODUCT_NAME "lemus-render")
target_sources(lemus-render PRIVATE ${LEMUS_HOST_DIR}/render/main.cpp)
lemus_add_host(lemus-render)

# The A/B player (a window): opens a scene and switches between its sources while they play.
# Its icon is ab/icon.svg, rendered with `rsvg-convert -w 1024 -h 1024 icon.svg -o icon.png`.
juce_add_gui_app(lemus-ab PRODUCT_NAME "Lemus AB" BUNDLE_ID "com.nonfatal.lemus.ab"
  ICON_BIG ${LEMUS_HOST_DIR}/ab/icon.png)
target_sources(lemus-ab PRIVATE ${LEMUS_HOST_DIR}/ab/Main.cpp)
lemus_add_host(lemus-ab EXTRA_LIBS
  juce::juce_audio_utils
  juce::juce_dsp
  juce::juce_gui_extra
)
