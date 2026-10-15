# The test host: lemus-render plays scenes (tools/host/README.md) through plugins offline; the
# A/B player plays them for listening.

set(LEMUS_HOST_DIR ${CMAKE_CURRENT_LIST_DIR})

juce_add_console_app(lemus-render PRODUCT_NAME "lemus-render")
target_sources(lemus-render PRIVATE
  ${LEMUS_HOST_DIR}/render/main.cpp
  ${LEMUS_HOST_DIR}/Player.cpp
  ${LEMUS_HOST_DIR}/Scene.cpp
)
target_compile_definitions(lemus-render PRIVATE
  JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
  LEMUS_HOST_DIR="${LEMUS_HOST_DIR}" LEMUS_MACHINES_DIR="${LEMUS_MACHINES_DIR}")
target_link_libraries(lemus-render PRIVATE
  juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags
  "-framework CoreAudioKit") # AU hosting (AUGenericView)

# The A/B player (a window): opens a scene and switches between its sources while they play.
# Its icon is ab/icon.svg, rendered with `rsvg-convert -w 1024 -h 1024 icon.svg -o icon.png`.
juce_add_gui_app(lemus-ab PRODUCT_NAME "Lemus AB" BUNDLE_ID "com.nonfatal.lemus.ab"
  ICON_BIG ${LEMUS_HOST_DIR}/ab/icon.png)
target_sources(lemus-ab PRIVATE
  ${LEMUS_HOST_DIR}/ab/Main.cpp
  ${LEMUS_HOST_DIR}/Player.cpp
  ${LEMUS_HOST_DIR}/Scene.cpp
)
target_compile_definitions(lemus-ab PRIVATE
  JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
  LEMUS_HOST_DIR="${LEMUS_HOST_DIR}" LEMUS_MACHINES_DIR="${LEMUS_MACHINES_DIR}")
target_link_libraries(lemus-ab PRIVATE
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp
  juce::juce_gui_extra juce::juce_recommended_config_flags "-framework CoreAudioKit")
