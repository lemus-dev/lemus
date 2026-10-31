cmake_minimum_required(VERSION 3.22)
project(LEMUS VERSION 0.1.4 LANGUAGES C CXX)

# ===========================================================================
# Global settings: the same on every platform.
# ===========================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ===========================================================================
# Platform decisions, taken once and used everywhere below.
# ===========================================================================

# Apple. Do not pin CMAKE_OSX_ARCHITECTURES: "arm64" breaks Intel Macs and CI on
# x86_64. Unset means "this machine's architecture", which is what people want by
# default; a universal build passes it explicitly:
#   -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# AU is an Apple-only format and standalone apps are not a universal target either;
# asking JUCE for AU on Linux/Windows does nothing useful.
if(APPLE)
  set(LEMUS_PLUGIN_FORMATS AU VST3)
else()
  set(LEMUS_PLUGIN_FORMATS VST3)
endif()

# Copying bundles into ~/Library/Audio/Plug-Ins is a macOS convention. Elsewhere the
# install step is the way in, so the default follows the platform.
if(APPLE)
  set(_lemus_copy_default ON)
else()
  set(_lemus_copy_default OFF)
endif()
option(LEMUS_COPY_PLUGINS "Copy the built plugins to the user's plug-in folder" ${_lemus_copy_default})

# MSVC and clang-cl need /utf-8 for the sources' UTF-8 literals.
if(MSVC OR "${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}" STREQUAL "MSVC")
  set(LEMUS_NEEDS_UTF8 ON)
else()
  set(LEMUS_NEEDS_UTF8 OFF)
endif()

# ===========================================================================
# JUCE and this project's directories.
# ===========================================================================
set(JUCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE CACHE PATH "JUCE source tree")
if(NOT EXISTS ${JUCE_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "JUCE is missing: git submodule update --init")
endif()
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)

# Material this repo must not contain (ROMs, artwork packs, manuals, reference recordings) lives
# in a private folder next to it. Builds don't need it; the tests and tools find ROMs, artwork
# packs and reference data there.
get_filename_component(LEMUS_PRIVATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../private" ABSOLUTE)
set(LEMUS_PRIVATE_DIR ${LEMUS_PRIVATE_DIR} CACHE PATH "Private material, never in this repo")
set(LEMUS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LEMUS_MACHINES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/machines)
set(LEMUS_FONTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts)
set(LEMUS_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)

# JUCE features no target here uses.
set(LEMUS_DEFINITIONS
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

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

# ===========================================================================
# Machines register themselves: adding or changing one touches only machines/<id>/.
# Every machines/<id>/machine.cmake is included, calls lemus_machine(<id> instrument|fx) and
# defines lemus_add_<id>(target) to add itself to a target. The driver declarations
# (machines/drivers.h) and each host's machine list (machines/<host>.cpp) are generated from that.
# ===========================================================================
set(LEMUS_INSTRUMENT_MACHINES)
set(LEMUS_FX_MACHINES)

function(lemus_machine id host)
  if(NOT "${host}" MATCHES "^(instrument|fx)$")
    message(FATAL_ERROR "lemus_machine(${id} ${host}): the host is instrument or fx")
  endif()
  string(TOUPPER "${host}" _upper)
  set(LEMUS_${_upper}_MACHINES ${LEMUS_${_upper}_MACHINES} ${id} PARENT_SCOPE)
endfunction()

# A machine's own plugin: the host with that machine alone, under the machine's own name (maker,
# kind, model, all lower case: "ro-mono-303"), a download of its own. Its machine.cmake declares
# it, after lemus_machine():
#   lemus_plugin(<id> NAME <name> CODE <plugin code> DESCRIPTION <text> [STANDALONE] [MIDI_OUT])
# The name and code are permanent once the plugin ships: sessions find the plugin by them. The
# code is four characters, an upper-case letter and then none. STANDALONE adds the standalone app.
# MIDI_OUT makes the plugin send MIDI: what the machine's processBlock leaves in its MidiBuffer
# (only its output: it takes the input out).
function(lemus_plugin id)
  cmake_parse_arguments(P "STANDALONE;MIDI_OUT" "NAME;CODE;DESCRIPTION" "" ${ARGN})
  if(NOT P_NAME OR NOT P_DESCRIPTION OR NOT "${P_CODE}" MATCHES "^[A-Z][^A-Z][^A-Z][^A-Z]$")
    message(FATAL_ERROR "lemus_plugin(${id}): needs a NAME, a DESCRIPTION and a CODE of four characters, only the first upper-case")
  endif()
  if(NOT "${P_NAME}" MATCHES "^[a-z]+-[a-z]+-[a-z0-9]+$")
    message(FATAL_ERROR "lemus_plugin(${id}): the NAME is <maker>-<kind>-<model>, all lower case (README, \"Plugin names\")")
  endif()
  set(LEMUS_PLUGIN_${id}_NAME "${P_NAME}" PARENT_SCOPE)
  set(LEMUS_PLUGIN_${id}_CODE "${P_CODE}" PARENT_SCOPE)
  set(LEMUS_PLUGIN_${id}_DESCRIPTION "${P_DESCRIPTION}" PARENT_SCOPE)
  set(LEMUS_PLUGIN_${id}_STANDALONE ${P_STANDALONE} PARENT_SCOPE)
  set(LEMUS_PLUGIN_${id}_MIDI_OUT ${P_MIDI_OUT} PARENT_SCOPE)
  set(LEMUS_PLUGIN_MACHINES ${LEMUS_PLUGIN_MACHINES} ${id} PARENT_SCOPE)
endfunction()

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

# ===========================================================================
# Small helpers shared by every target.
# ===========================================================================

# The juce_add_plugin() arguments that depend on which host a plugin is.
function(lemus_kind_arguments host out)
  if("${host}" STREQUAL "instrument")
    set(_args IS_SYNTH TRUE AU_MAIN_TYPE kAudioUnitType_MusicDevice VST3_CATEGORIES Instrument Synth)
  elseif("${host}" STREQUAL "fx")
    set(_args IS_SYNTH FALSE AU_MAIN_TYPE kAudioUnitType_MusicEffect VST3_CATEGORIES Fx)
  else()
    message(FATAL_ERROR "unknown host '${host}': instrument or fx")
  endif()
  set(${out} ${_args} PARENT_SCOPE)
endfunction()

# 1 for an instrument, 0 for an effect; the test hosts declare JucePlugin_IsSynth with it.
function(lemus_is_synth host out)
  if("${host}" STREQUAL "instrument")
    set(${out} 1 PARENT_SCOPE)
  elseif("${host}" STREQUAL "fx")
    set(${out} 0 PARENT_SCOPE)
  else()
    message(FATAL_ERROR "unknown host '${host}': instrument or fx")
  endif()
endfunction()

# Whether a machine is an instrument or an effect, from its lemus_machine().
function(lemus_host_of id out)
  if(id IN_LIST LEMUS_INSTRUMENT_MACHINES)
    set(${out} instrument PARENT_SCOPE)
  elseif(id IN_LIST LEMUS_FX_MACHINES)
    set(${out} fx PARENT_SCOPE)
  else()
    message(FATAL_ERROR "lemus_host_of(${id}): the machine is not registered")
  endif()
endfunction()

# Every setting every target gets: the JUCE features off, the site and source, the recommended
# flags, and /utf-8 where the compiler needs it. One place, so plugins and test hosts agree.
function(lemus_configure_target target)
  target_compile_definitions(${target} PUBLIC
    ${LEMUS_DEFINITIONS}
    LEMUS_SITE="${LEMUS_SITE}"
    LEMUS_SOURCE="${LEMUS_SOURCE}"
    LEMUS_UPDATE_CHECK=1)
  if(LEMUS_NEEDS_UTF8)
    target_compile_options(${target} PRIVATE /utf-8)
  endif()
  target_link_libraries(${target} PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
endfunction()

# Windows version info (host/version.rc.in): the product is Lemus, the description the plugin.
# A no-op elsewhere, so callers don't need to know the platform.
function(lemus_add_windows_resources target description)
  if(NOT WIN32)
    return()
  endif()
  string(REPLACE "." "," _lemus_rc_version "${PROJECT_VERSION}.0")
  set(_lemus_rc_name "${description}")
  configure_file(${LEMUS_ROOT_DIR}/host/version.rc.in ${LEMUS_GENERATED_DIR}/rc/${target}.rc @ONLY)
  target_compile_definitions(${target} PRIVATE
    JUCE_USER_DEFINED_RC_FILE="${LEMUS_GENERATED_DIR}/rc/${target}.rc")
endfunction()

# The generated driver declarations and machine lists.
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

# ===========================================================================
# The shared host: its sources, its font data, and how a plugin gets it.
# ===========================================================================
set(LEMUS_HOST_SOURCES
  host/LemusProcessor.cpp
  host/LemusEditor.cpp
  host/SlotParameter.cpp
  host/Roms.cpp
  host/Updates.cpp
  host/Style.cpp
)

# The font of Lemus's own screens (host/Style.h), SIL OFL.
foreach(_font JetBrainsMono-Regular.ttf JetBrainsMono-Bold.ttf)
  if(NOT EXISTS ${LEMUS_FONTS_DIR}/${_font})
    message(FATAL_ERROR "Missing font ${_font}: git submodule update --init")
  endif()
endforeach()
juce_add_binary_data(lemus_host_fonts HEADER_NAME LemusFonts.h NAMESPACE LemusFonts SOURCES
  ${LEMUS_FONTS_DIR}/JetBrainsMono-Regular.ttf
  ${LEMUS_FONTS_DIR}/JetBrainsMono-Bold.ttf
)

# The shared host with the machines given after its machine list (machines/<list>.cpp).
function(lemus_add_host_with target list)
  target_sources(${target} PRIVATE
    ${LEMUS_HOST_SOURCES}
    ${LEMUS_GENERATED_DIR}/machines/${list}.cpp)
  target_include_directories(${target} PRIVATE
    ${LEMUS_ROOT_DIR} ${LEMUS_MACHINES_DIR} ${LEMUS_GENERATED_DIR})
  target_link_libraries(${target} PRIVATE
    juce::juce_audio_utils juce::juce_audio_processors lemus_host_fonts)
  foreach(id ${ARGN})
    cmake_language(CALL lemus_add_${id} ${target})
  endforeach()
endfunction()

# The shared host with its machines (host: instrument or fx).
function(lemus_add_host target host)
  string(TOUPPER ${host} HOST)
  lemus_add_host_with(${target} ${host} ${LEMUS_${HOST}_MACHINES})
endfunction()

# ===========================================================================
# Plugins. One function makes every plugin, the two hosts and each machine's own.
# ===========================================================================
set(LEMUS_PLUGINS)

#   lemus_add_plugin(<target>
#     HOST <instrument|fx> NAME <product name> CODE <4 characters> DESCRIPTION <text>
#     BUNDLE_ID <id> [ICON <png>] [STANDALONE] [MIDI_OUT]
#     [SOURCES <machine list>] [MACHINES <id>...])
#
# SOURCES/MACHINES default to the host's own list (machines/<host>.cpp and every machine of that
# host). ICON defaults to host/icon.png; the per-machine plugins pass machines/<id>/icon.png.
# Adds the target to LEMUS_PLUGINS.
function(lemus_add_plugin target)
  cmake_parse_arguments(P "STANDALONE;MIDI_OUT"
    "HOST;NAME;CODE;BUNDLE_ID;DESCRIPTION;ICON;SOURCES" "MACHINES" ${ARGN})

  foreach(_required HOST NAME CODE BUNDLE_ID DESCRIPTION)
    if(NOT P_${_required})
      message(FATAL_ERROR "lemus_add_plugin(${target}): ${_required} is required")
    endif()
  endforeach()
  if(NOT "${P_CODE}" MATCHES "^[A-Z][^A-Z][^A-Z][^A-Z]$")
    message(FATAL_ERROR "lemus_add_plugin(${target}): CODE is four characters, only the first upper-case")
  endif()

  if(P_SOURCES)
    set(_list "${P_SOURCES}")
    set(_machines ${P_MACHINES})
  else()
    string(TOUPPER "${P_HOST}" _upper)
    set(_list "${P_HOST}")
    set(_machines ${LEMUS_${_upper}_MACHINES})
  endif()

  set(_formats ${LEMUS_PLUGIN_FORMATS})
  if(P_STANDALONE)
    list(APPEND _formats Standalone)
  endif()
  if(P_MIDI_OUT)
    set(_midi_out TRUE)
  else()
    set(_midi_out FALSE)
  endif()

  set(_icon ${LEMUS_ROOT_DIR}/host/icon.png)
  if(P_ICON AND EXISTS "${P_ICON}")
    set(_icon "${P_ICON}")
  endif()

  lemus_kind_arguments("${P_HOST}" _kind)

  juce_add_plugin(${target}
    COMPANY_NAME "Lemus"
    BUNDLE_ID "${P_BUNDLE_ID}"
    ICON_BIG "${_icon}"
    PLUGIN_MANUFACTURER_CODE Lmus
    PLUGIN_CODE "${P_CODE}"
    FORMATS ${_formats}
    PRODUCT_NAME "${P_NAME}"
    DESCRIPTION "${P_DESCRIPTION}"
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT ${_midi_out}
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${LEMUS_COPY_PLUGINS}
    ${_kind})

  lemus_add_host_with(${target} ${_list} ${_machines})
  lemus_configure_target(${target})
  lemus_add_windows_resources(${target} "${P_NAME}")

  set(LEMUS_PLUGINS ${LEMUS_PLUGINS} ${target} PARENT_SCOPE)
endfunction()

# Lemus: the instruments host.
if(LEMUS_INSTRUMENT_MACHINES)
  lemus_add_plugin(Lemus
    HOST instrument
    NAME "Lemus"
    CODE Lmin
    BUNDLE_ID "org.lemusdev.lemus"
    DESCRIPTION "Emulations of legacy instruments"
    STANDALONE)
endif()

# Lemus FX: the effects host. MIDI in, for machines that take program changes and controllers.
if(LEMUS_FX_MACHINES)
  lemus_add_plugin(LemusFX
    HOST fx
    NAME "Lemus FX"
    CODE Lmfx
    BUNDLE_ID "org.lemusdev.lemusfx"
    DESCRIPTION "Emulations of legacy studio effects"
    STANDALONE)
endif()

# Each machine's own plugin (lemus_plugin): target plugin_<id>, its bundles named after the plugin
# (plugin_tb303_artefacts/Release/VST3/ro-mono-303.vst3).
foreach(id ${LEMUS_PLUGIN_MACHINES})
  lemus_host_of(${id} _host)
  set(_args
    HOST ${_host}
    NAME "${LEMUS_PLUGIN_${id}_NAME}"
    CODE ${LEMUS_PLUGIN_${id}_CODE}
    BUNDLE_ID "org.lemusdev.${id}"
    DESCRIPTION "${LEMUS_PLUGIN_${id}_DESCRIPTION}"
    ICON ${LEMUS_MACHINES_DIR}/${id}/icon.png
    SOURCES only_${id}
    MACHINES ${id})
  if(LEMUS_PLUGIN_${id}_STANDALONE)
    list(APPEND _args STANDALONE)
  endif()
  if(LEMUS_PLUGIN_${id}_MIDI_OUT)
    list(APPEND _args MIDI_OUT)
  endif()
  lemus_add_plugin(plugin_${id} ${_args})
  target_compile_definitions(plugin_${id} PUBLIC LEMUS_MACHINE="${id}")
endforeach()

# ===========================================================================
# Headless checks.
# ===========================================================================

# A program of tests/<source>.cpp with the host and the machines given after its machine list
# (machines/<list>.cpp), built as the plugin `name` (an instrument if is_synth is 1).
function(lemus_add_check target source list is_synth name)
  juce_add_console_app(${target} PRODUCT_NAME ${target})
  target_sources(${target} PRIVATE ${LEMUS_ROOT_DIR}/tests/${source}.cpp)
  target_include_directories(${target} PRIVATE ${LEMUS_ROOT_DIR}/tests)
  lemus_add_host_with(${target} ${list} ${ARGN})
  # The plugin the host is pretending to be: the code reads these as if it were built as one.
  target_compile_definitions(${target} PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="${name}"
    JucePlugin_VersionString="${PROJECT_VERSION}"
    JucePlugin_IsSynth=${is_synth}
    JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
    LEMUS_PRIVATE_DIR="${LEMUS_PRIVATE_DIR}"
    LEMUS_ROOT_DIR="${LEMUS_ROOT_DIR}")
  lemus_configure_target(${target})
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

if(LEMUS_TOOLS)
  foreach(host instrument fx)
    string(TOUPPER ${host} HOST)
    if(NOT LEMUS_${HOST}_MACHINES)
      continue()
    endif()
    lemus_is_synth(${host} _synth)
    if(host STREQUAL "instrument")
      set(_name "Lemus")
    else()
      set(_name "Lemus FX")
    endif()
    lemus_add_test(lemustest_${host} ${host} ${_synth} "${_name}")
  endforeach()

  # Each machine's own plugin, checked as it ships (tests/plugin.cpp):
  #   build/<dir>/lemustest_<id>_artefacts/Release/lemustest_<id>
  foreach(id ${LEMUS_PLUGIN_MACHINES})
    lemus_host_of(${id} _host)
    lemus_is_synth(${_host} _synth)
    lemus_add_check(lemustest_${id} plugin only_${id} ${_synth} "${LEMUS_PLUGIN_${id}_NAME}" ${id})
    target_compile_definitions(lemustest_${id} PRIVATE LEMUS_MACHINE="${id}")
  endforeach()
endif()
