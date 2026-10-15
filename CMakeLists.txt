cmake_minimum_required(VERSION 3.22)
project(LEMUS VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(JUCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE CACHE PATH "JUCE source tree")
if(NOT EXISTS ${JUCE_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "JUCE is missing: git submodule update --init")
endif()
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)

# Material this repo must not contain (ROMs, artwork packs, manuals, reference recordings) lives
# in a private folder next to it. Builds don't need it; the tests and tools find ROMs, artwork
# packs and reference data there.
set(LEMUS_PRIVATE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../private CACHE PATH "Private material, never in this repo")
set(LEMUS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LEMUS_MACHINES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/machines)
set(LEMUS_FONTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts)
set(LEMUS_DEFINITIONS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)

# The machines in this build, by id (their folder names), or empty for all of them. A release
# lists the machines that ship: -DLEMUS_MACHINES=tb303. A host with no machines isn't built.
set(LEMUS_MACHINES "" CACHE STRING "Machines in this build (ids separated by ;), empty for all")
# The test host and the hosts' tests. A release build leaves them out.
option(LEMUS_TOOLS "Build the test host and the hosts' tests" ON)
# The project's website: the plugins link to it and read the latest version from it. Not a cache
# variable, so changing it here reaches every existing build directory.
set(LEMUS_SITE "https://lemusdev.org")
# Where the source is published (AGPLv3): the About box links to it.
set(LEMUS_SOURCE "https://github.com/lemus-dev/lemus")

# Machines register themselves, so adding or changing one touches only machines/<id>/: every
# machines/<id>/machine.cmake is included, calls lemus_machine(<id> instrument|fx) and defines
# lemus_add_<id>(target) to add itself to a target. The driver declarations
# (machines/drivers.h) and each host's machine list (machines/<host>.cpp) are generated from that.
set(LEMUS_INSTRUMENT_MACHINES)
set(LEMUS_FX_MACHINES)
macro(lemus_machine id host)
  if(NOT "${host}" MATCHES "^(instrument|fx)$")
    message(FATAL_ERROR "lemus_machine(${id} ${host}): the host is instrument or fx")
  endif()
  string(TOUPPER "${host}" _lemus_host)
  list(APPEND LEMUS_${_lemus_host}_MACHINES ${id})
endmacro()
# A machine's own plugin: the host with that machine alone, under the machine's own name (maker,
# kind, model, all lower case: "ro-mono-303"), a download of its own. Its machine.cmake declares
# it, after lemus_machine():
#   lemus_plugin(<id> NAME <name> CODE <plugin code> DESCRIPTION <text> [STANDALONE])
# The name and code are permanent once the plugin ships: sessions find the plugin by them. The
# code is four characters, an upper-case letter and then none. STANDALONE adds the standalone app.
set(LEMUS_PLUGIN_MACHINES)
macro(lemus_plugin id)
  cmake_parse_arguments(_lemus_plugin "STANDALONE" "NAME;CODE;DESCRIPTION" "" ${ARGN})
  if(NOT _lemus_plugin_NAME OR NOT _lemus_plugin_DESCRIPTION OR NOT "${_lemus_plugin_CODE}" MATCHES "^[A-Z][^A-Z][^A-Z][^A-Z]$")
    message(FATAL_ERROR "lemus_plugin(${id}): needs a NAME, a DESCRIPTION and a CODE of four characters, only the first upper-case")
  endif()
  if(NOT "${_lemus_plugin_NAME}" MATCHES "^[a-z]+-[a-z]+-[a-z0-9]+$")
    message(FATAL_ERROR "lemus_plugin(${id}): the NAME is <maker>-<kind>-<model>, all lower case (README, \"Plugin names\")")
  endif()
  set(LEMUS_PLUGIN_${id}_NAME "${_lemus_plugin_NAME}")
  set(LEMUS_PLUGIN_${id}_CODE "${_lemus_plugin_CODE}")
  set(LEMUS_PLUGIN_${id}_DESCRIPTION "${_lemus_plugin_DESCRIPTION}")
  set(LEMUS_PLUGIN_${id}_STANDALONE ${_lemus_plugin_STANDALONE})
  list(APPEND LEMUS_PLUGIN_MACHINES ${id})
endmacro()
file(GLOB LEMUS_MACHINE_FILES CONFIGURE_DEPENDS ${LEMUS_MACHINES_DIR}/*/machine.cmake)
list(SORT LEMUS_MACHINE_FILES)
set(_lemus_found)
foreach(machine_file ${LEMUS_MACHINE_FILES})
  get_filename_component(_lemus_id ${machine_file} DIRECTORY)
  get_filename_component(_lemus_id ${_lemus_id} NAME)
  list(APPEND _lemus_found ${_lemus_id})
  if(NOT LEMUS_MACHINES OR _lemus_id IN_LIST LEMUS_MACHINES)
    include(${machine_file})
  endif()
endforeach()
foreach(id ${LEMUS_MACHINES})
  if(NOT id IN_LIST _lemus_found)
    message(FATAL_ERROR "LEMUS_MACHINES: there is no machines/${id}/machine.cmake")
  endif()
endforeach()
message(STATUS "Lemus machines: ${LEMUS_INSTRUMENT_MACHINES} (instrument), ${LEMUS_FX_MACHINES} (fx)")

set(LEMUS_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
set(LEMUS_DRIVER_DECLARATIONS "")
foreach(id ${LEMUS_INSTRUMENT_MACHINES} ${LEMUS_FX_MACHINES})
  string(APPEND LEMUS_DRIVER_DECLARATIONS "const MachineInfo &${id}();\n")
endforeach()
file(CONFIGURE OUTPUT ${LEMUS_GENERATED_DIR}/machines/drivers.h @ONLY CONTENT
"// Generated by CMakeLists.txt: every machine's driver, one function per machine, defined in
// machines/<id>/<id>.cpp.
#pragma once

#include \"host/Machine.h\"

namespace lemus::drivers {

@LEMUS_DRIVER_DECLARATIONS@
} // namespace lemus::drivers
")
# A machine list (machines/<name>.cpp): the machines a build of the host has.
function(lemus_machine_list name what)
  set(LEMUS_MACHINE_CALLS "")
  foreach(id ${ARGN})
    string(APPEND LEMUS_MACHINE_CALLS "drivers::${id}(), ")
  endforeach()
  file(CONFIGURE OUTPUT ${LEMUS_GENERATED_DIR}/machines/${name}.cpp @ONLY CONTENT
"// Generated by CMakeLists.txt: the machines in @what@, from the machine.cmake files.
#include \"machines/drivers.h\"

namespace lemus {

const std::vector<MachineInfo> &machines()
{
	static const std::vector<MachineInfo> list{@LEMUS_MACHINE_CALLS@};
	return list;
}

} // namespace lemus
")
endfunction()
foreach(host instrument fx)
  string(TOUPPER ${host} HOST)
  lemus_machine_list(${host} "the ${host} host" ${LEMUS_${HOST}_MACHINES})
endforeach()
foreach(id ${LEMUS_PLUGIN_MACHINES})
  lemus_machine_list(only_${id} "${LEMUS_PLUGIN_${id}_NAME}, the ${id} alone" ${id})
endforeach()

if(LEMUS_TOOLS)
  include(tools/host/host.cmake)
endif()

set(LEMUS_HOST_SOURCES
  host/LemusProcessor.cpp
  host/LemusEditor.cpp
  host/SlotParameter.cpp
  host/Roms.cpp
  host/Updates.cpp
  host/Style.cpp
)

# The font of Lemus's own screens (host/Style.h), SIL OFL.
juce_add_binary_data(lemus_host_fonts HEADER_NAME LemusFonts.h NAMESPACE LemusFonts SOURCES
  ${LEMUS_FONTS_DIR}/JetBrainsMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/JetBrainsMono-Bold.ttf
)

# The shared host with the machines given after its machine list (machines/<list>.cpp).
function(lemus_add_host_with target list)
  target_sources(${target} PRIVATE ${LEMUS_HOST_SOURCES} ${LEMUS_GENERATED_DIR}/machines/${list}.cpp)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${LEMUS_MACHINES_DIR} ${LEMUS_GENERATED_DIR})
  target_link_libraries(${target} PRIVATE juce::juce_audio_utils juce::juce_audio_processors lemus_host_fonts)
  foreach(id ${ARGN})
    cmake_language(CALL lemus_add_${id} ${target})
  endforeach()
endfunction()

# The shared host with its machines (host: instrument or fx).
function(lemus_add_host target host)
  string(TOUPPER ${host} HOST)
  lemus_add_host_with(${target} ${host} ${LEMUS_${HOST}_MACHINES})
endfunction()

# Whether a machine is an instrument or an effect, from its lemus_machine().
function(lemus_host_of id out)
  if(id IN_LIST LEMUS_INSTRUMENT_MACHINES)
    set(${out} instrument PARENT_SCOPE)
  else()
    set(${out} fx PARENT_SCOPE)
  endif()
endfunction()

# OFF while a host has the plugins loaded: copying over a loaded binary can crash the host.
option(LEMUS_COPY_PLUGINS "Copy the AU and VST3 to ~/Library/Audio/Plug-Ins after building" ON)

set(LEMUS_PLUGINS)

# Lemus: the instruments host.
if(LEMUS_INSTRUMENT_MACHINES)
juce_add_plugin(Lemus
  COMPANY_NAME "Lemus"
  BUNDLE_ID "org.lemusdev.lemus"
  ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/host/icon.png
  PLUGIN_MANUFACTURER_CODE Lmus
  PLUGIN_CODE Lmin
  FORMATS AU VST3 Standalone
  PRODUCT_NAME "Lemus"
  DESCRIPTION "Emulations of legacy instruments"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD ${LEMUS_COPY_PLUGINS}
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Synth
)
lemus_add_host(Lemus instrument)
list(APPEND LEMUS_PLUGINS Lemus)
endif()

# Lemus FX: the effects host. MIDI in, for machines that take program changes and controllers.
if(LEMUS_FX_MACHINES)
juce_add_plugin(LemusFX
  COMPANY_NAME "Lemus"
  BUNDLE_ID "org.lemusdev.lemusfx"
  ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/host/icon.png
  PLUGIN_MANUFACTURER_CODE Lmus
  PLUGIN_CODE Lmfx
  FORMATS AU VST3 Standalone
  PRODUCT_NAME "Lemus FX"
  DESCRIPTION "Emulations of legacy studio effects"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD ${LEMUS_COPY_PLUGINS}
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  VST3_CATEGORIES Fx
)
lemus_add_host(LemusFX fx)
list(APPEND LEMUS_PLUGINS LemusFX)
endif()

# Each machine's own plugin (lemus_plugin): target plugin_<id>, its bundles named after the plugin
# (plugin_tb303_artefacts/Release/VST3/ro-mono-303.vst3).
foreach(id ${LEMUS_PLUGIN_MACHINES})
  if(NOT id IN_LIST LEMUS_INSTRUMENT_MACHINES AND NOT id IN_LIST LEMUS_FX_MACHINES)
    message(FATAL_ERROR "lemus_plugin(${id}): call lemus_machine(${id} ...) first")
  endif()
  lemus_host_of(${id} _lemus_host)
  if(_lemus_host STREQUAL "instrument")
    set(_lemus_kind IS_SYNTH TRUE AU_MAIN_TYPE kAudioUnitType_MusicDevice VST3_CATEGORIES Instrument Synth)
  else()
    set(_lemus_kind IS_SYNTH FALSE AU_MAIN_TYPE kAudioUnitType_MusicEffect VST3_CATEGORIES Fx)
  endif()
  set(_lemus_formats AU VST3)
  if(LEMUS_PLUGIN_${id}_STANDALONE)
    list(APPEND _lemus_formats Standalone)
  endif()
  # The standalone app's icon: machines/<id>/icon.png (tools/icon/make_icon.py), else the Lemus mark.
  set(_lemus_icon ${LEMUS_MACHINES_DIR}/${id}/icon.png)
  if(NOT EXISTS ${_lemus_icon})
    set(_lemus_icon ${CMAKE_CURRENT_SOURCE_DIR}/host/icon.png)
  endif()
  juce_add_plugin(plugin_${id}
    COMPANY_NAME "Lemus"
    BUNDLE_ID "org.lemusdev.${id}"
    ICON_BIG ${_lemus_icon}
    PLUGIN_MANUFACTURER_CODE Lmus
    PLUGIN_CODE ${LEMUS_PLUGIN_${id}_CODE}
    FORMATS ${_lemus_formats}
    PRODUCT_NAME "${LEMUS_PLUGIN_${id}_NAME}"
    DESCRIPTION "${LEMUS_PLUGIN_${id}_DESCRIPTION}"
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${LEMUS_COPY_PLUGINS}
    ${_lemus_kind}
  )
  lemus_add_host_with(plugin_${id} only_${id} ${id})
  target_compile_definitions(plugin_${id} PUBLIC LEMUS_MACHINE="${id}")
  list(APPEND LEMUS_PLUGINS plugin_${id})
endforeach()

foreach(target ${LEMUS_PLUGINS})
  target_compile_definitions(${target} PUBLIC ${LEMUS_DEFINITIONS} LEMUS_SITE="${LEMUS_SITE}" LEMUS_SOURCE="${LEMUS_SOURCE}" LEMUS_UPDATE_CHECK=1)
  target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
  target_link_libraries(${target} PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
endforeach()

# Headless checks: a program of tests/<source>.cpp with the host and the machines given after its
# machine list (machines/<list>.cpp), built as the plugin `name` (an instrument if is_synth is 1).
function(lemus_add_check target source list is_synth name)
  juce_add_console_app(${target} PRODUCT_NAME ${target})
  target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/${source}.cpp)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  lemus_add_host_with(${target} ${list} ${ARGN})
  target_compile_definitions(${target} PRIVATE ${LEMUS_DEFINITIONS}
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="${name}"
    JucePlugin_VersionString="${PROJECT_VERSION}"
    JucePlugin_IsSynth=${is_synth}
    JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
    LEMUS_PRIVATE_DIR="${LEMUS_PRIVATE_DIR}"
    LEMUS_ROOT_DIR="${LEMUS_ROOT_DIR}"
    LEMUS_SITE="${LEMUS_SITE}"
    LEMUS_SOURCE="${LEMUS_SOURCE}")
  target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
  target_link_libraries(${target} PRIVATE juce::juce_recommended_config_flags)
endfunction()

# Each host with its machines: the host's own checks (tests/<host>.cpp) and each machine's through
# it (machines/<id>/tests/host.cpp, registered with LEMUS_HOST_TEST):
#   build/<dir>/lemustest_fx_artefacts/Release/lemustest_fx
#   build/<dir>/lemustest_instrument_artefacts/Release/lemustest_instrument   (ROM tests need the private folder)
function(lemus_add_test target host is_synth name)
  string(TOUPPER ${host} HOST)
  lemus_add_check(${target} ${host} ${host} ${is_synth} "${name}" ${LEMUS_${HOST}_MACHINES})
  foreach(id ${LEMUS_${HOST}_MACHINES})
    if(EXISTS ${LEMUS_MACHINES_DIR}/${id}/tests/host.cpp)
      target_sources(${target} PRIVATE ${LEMUS_MACHINES_DIR}/${id}/tests/host.cpp)
    endif()
  endforeach()
endfunction()
if(LEMUS_TOOLS AND LEMUS_FX_MACHINES)
  lemus_add_test(lemustest_fx fx 0 "Lemus FX")
endif()
if(LEMUS_TOOLS AND LEMUS_INSTRUMENT_MACHINES)
  lemus_add_test(lemustest_instrument instrument 1 "Lemus")
endif()

# Each machine's own plugin, checked as it ships (tests/plugin.cpp):
#   build/<dir>/lemustest_<id>_artefacts/Release/lemustest_<id>
if(LEMUS_TOOLS)
  foreach(id ${LEMUS_PLUGIN_MACHINES})
    lemus_host_of(${id} _lemus_host)
    if(_lemus_host STREQUAL "instrument")
      set(_lemus_synth 1)
    else()
      set(_lemus_synth 0)
    endif()
    lemus_add_check(lemustest_${id} plugin only_${id} ${_lemus_synth} "${LEMUS_PLUGIN_${id}_NAME}" ${id})
    target_compile_definitions(lemustest_${id} PRIVATE LEMUS_MACHINE="${id}")
  endforeach()
endif()
