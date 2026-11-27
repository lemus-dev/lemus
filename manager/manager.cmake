# Lemus Plugin Manager (manager/README.md): an app that lists the released plugins, installs,
# updates and removes them from the same installers the website offers. Included by the top
# CMakeLists.txt after the host's sources and fonts are defined.
#
#   cmake --build build/manager --target lemus_manager lemustest_manager
#   build/manager/lemus_manager_artefacts/Release/Lemus Plugin Manager.app
#   build/manager/lemustest_manager_artefacts/Release/lemustest_manager

set(LEMUS_MANAGER_VERSION 0.2.3)
set(LEMUS_MANAGER_NAME "Lemus Plugin Manager")

# What the app and its checks share: the catalogue (latest.json), what is installed, the tasks.
set(LEMUS_MANAGER_CORE
  ${CMAKE_CURRENT_LIST_DIR}/Catalog.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Installed.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Tasks.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Report.cpp
  ${CMAKE_CURRENT_LIST_DIR}/RomCheck.cpp
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
  NEEDS_CURL TRUE
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

# What each shipping plugin needs from the ROM folder, as JSON for the website (manager/specs.cpp).
# Instruments and effects are separate hosts that don't link together, so there is one tool per
# host: lemus_specs_instrument and lemus_specs_fx, each printing its plugins, merged into
# website/src/data/roms.json. They link every machine of their host, so they are built only when
# asked for.
foreach(_lemus_host instrument fx)
  string(TOUPPER ${_lemus_host} _LEMUS_HOST)
  set(_lemus_specs_ids)
  foreach(id ${LEMUS_${_LEMUS_HOST}_MACHINES})
    if(id IN_LIST LEMUS_PLUGIN_MACHINES)
      list(APPEND _lemus_specs_ids ${id})
    endif()
  endforeach()
  if(_lemus_specs_ids)
    set(_lemus_target lemus_specs_${_lemus_host})
    lemus_machine_list(specs_${_lemus_host} "every shipping ${_lemus_host} plugin" ${_lemus_specs_ids})
    juce_add_console_app(${_lemus_target} PRODUCT_NAME ${_lemus_target})
    set_target_properties(${_lemus_target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources(${_lemus_target} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/specs.cpp)
    lemus_add_host_with(${_lemus_target} specs_${_lemus_host} ${_lemus_specs_ids})
    if(_lemus_host STREQUAL "instrument")
      set(_lemus_synth 1)
    else()
      set(_lemus_synth 0)
    endif()
    target_compile_definitions(${_lemus_target} PRIVATE ${LEMUS_DEFINITIONS}
      JucePlugin_Name="${_lemus_target}"
      JucePlugin_VersionString="${PROJECT_VERSION}"
      JucePlugin_IsSynth=${_lemus_synth}
      JucePlugin_WantsMidiInput=1
      JucePlugin_ProducesMidiOutput=0
      JucePlugin_IsMidiEffect=0
      LEMUS_SITE="${LEMUS_SITE}"
      LEMUS_SOURCE="${LEMUS_SOURCE}"
      LEMUS_FORUM="${LEMUS_FORUM}")
    target_compile_options(${_lemus_target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_link_libraries(${_lemus_target} PRIVATE juce::juce_recommended_config_flags)
  endif()
endforeach()

# Its checks, headless: build/<dir>/lemustest_manager_artefacts/Release/lemustest_manager
juce_add_console_app(lemustest_manager PRODUCT_NAME lemustest_manager)
target_sources(lemustest_manager PRIVATE ${CMAKE_CURRENT_LIST_DIR}/tests.cpp ${LEMUS_MANAGER_CORE})
lemus_manager_setup(lemustest_manager)
target_compile_definitions(lemustest_manager PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 LEMUS_ROOT_DIR="${LEMUS_ROOT_DIR}")
