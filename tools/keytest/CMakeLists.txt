# keyhost: computer keys by place, end to end, in a built plugin (AU or VST3 on macOS, VST3 on
# Windows). It loads the plugin through JUCE's hosting, presses the keys at the US Y and US Z
# places in the layout in use and in German, and compares window shots: each place must do the
# same in both layouts, and the two places different things. Its own project, not part of the
# main build:
#   cmake -S tools/keytest -B build/keytest && cmake --build build/keytest --config Release
#   build/keytest/keyhost_artefacts/Release/keyhost VST3 <plugin.vst3> <outdir> <clickX> <clickY>
# The click (editor points) is a blank part of the panel, for keyboard focus. macOS: Ghostty (or
# whatever runs it) needs Screen Recording, and the screen must be unlocked. Windows: run by
# .github/workflows/windows-keytest.yml.
cmake_minimum_required(VERSION 3.22)
project(keytest VERSION 1.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/JUCE ${CMAKE_BINARY_DIR}/JUCE)
juce_add_console_app(keyhost PRODUCT_NAME keyhost)
target_compile_definitions(keyhost PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(keyhost PRIVATE juce::juce_audio_processors juce::juce_audio_utils
  juce::juce_recommended_config_flags)
if(APPLE)
  target_sources(keyhost PRIVATE keyhost.mm tis.mm)
  target_compile_definitions(keyhost PRIVATE JUCE_PLUGINHOST_AU=1)
  target_link_libraries(keyhost PRIVATE "-framework Carbon")
elseif(WIN32)
  target_sources(keyhost PRIVATE keyhost_win.cpp win.cpp)
  target_compile_options(keyhost PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
  target_link_libraries(keyhost PRIVATE user32 gdi32)
endif()
