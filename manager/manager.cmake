# Lemus Plugin Manager (manager/README.md): an app that lists the released plugins, installs,
# updates and removes them from the same installers the website offers. Included by the top
# CMakeLists.txt after the host's sources and fonts are defined.
#
#   cmake --build build/manager --target lemus_manager lemustest_manager
#   build/manager/lemus_manager_artefacts/Release/Lemus Plugin Manager.app
#   build/manager/lemustest_manager_artefacts/Release/lemustest_manager

set(LEMUS_MANAGER_VERSION 0.1.0)
set(LEMUS_MANAGER_NAME "Lemus Plugin Manager")

# What the app and its checks share: the catalogue (latest.json), what is installed, the tasks.
set(LEMUS_MANAGER_CORE
  ${CMAKE_CURRENT_LIST_DIR}/Catalog.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Installed.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Tasks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/host/Updates.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/host/Roms.cpp
)

function(lemus_manager_setup target)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${target} PRIVATE ${LEMUS_DEFINITIONS}
    LEMUS_SITE="${LEMUS_SITE}"
    LEMUS_SOURCE="${LEMUS_SOURCE}"
    LEMUS_FORUM="${LEMUS_FORUM}"
    LEMUS_MANAGER_VERSION="${LEMUS_MANAGER_VERSION}"
    LEMUS_UPDATE_CHECK=0
    # host/Updates.cpp keys its pop-up settings by plugin name; the manager has its own.
    JucePlugin_Name="${LEMUS_MANAGER_NAME}"
    JucePlugin_VersionString="${LEMUS_MANAGER_VERSION}"
    JUCE_APPLICATION_NAME_STRING="${LEMUS_MANAGER_NAME}"
    JUCE_APPLICATION_VERSION_STRING="${LEMUS_MANAGER_VERSION}")
  target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
  target_link_libraries(${target} PRIVATE
    juce::juce_gui_basics
    juce::juce_cryptography
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
  if(WIN32)
    target_link_libraries(${target} PRIVATE shell32 version)
  endif()
endfunction()

juce_add_gui_app(lemus_manager
  PRODUCT_NAME "${LEMUS_MANAGER_NAME}"
  COMPANY_NAME "Lemus"
  BUNDLE_ID org.lemusdev.pluginmanager
  VERSION ${LEMUS_MANAGER_VERSION}
  ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/host/icon.png
)
target_sources(lemus_manager PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/Main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/MainComponent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/host/Style.cpp
  ${LEMUS_MANAGER_CORE})
lemus_manager_setup(lemus_manager)
target_link_libraries(lemus_manager PRIVATE lemus_host_fonts juce::juce_recommended_lto_flags)
if(WIN32)
  # Windows version info: the product is Lemus, the description the app.
  set(_lemus_rc_text "${LEMUS_MANAGER_VERSION}")
  string(REPLACE "." "," _lemus_rc_version "${LEMUS_MANAGER_VERSION}.0")
  set(_lemus_rc_name "${LEMUS_MANAGER_NAME}")
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/host/version.rc.in ${LEMUS_GENERATED_DIR}/rc/manager.rc @ONLY)
  target_compile_definitions(lemus_manager PRIVATE JUCE_USER_DEFINED_RC_FILE="${LEMUS_GENERATED_DIR}/rc/manager.rc")
  # The DLLs the exe imports load from System32 only, never from the folder it was started in (a
  # Downloads folder may hold a planted version.dll): LOAD_LIBRARY_SEARCH_SYSTEM32. Main.cpp sets
  # the same for the DLLs loaded later.
  if(MSVC)
    target_link_options(lemus_manager PRIVATE /DEPENDENTLOADFLAG:0x800)
  endif()
endif()

# Its checks, headless: build/<dir>/lemustest_manager_artefacts/Release/lemustest_manager
juce_add_console_app(lemustest_manager PRODUCT_NAME lemustest_manager)
target_sources(lemustest_manager PRIVATE ${CMAKE_CURRENT_LIST_DIR}/tests.cpp ${LEMUS_MANAGER_CORE})
lemus_manager_setup(lemustest_manager)
target_compile_definitions(lemustest_manager PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 LEMUS_ROOT_DIR="${LEMUS_ROOT_DIR}")
