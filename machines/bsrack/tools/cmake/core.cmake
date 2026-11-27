# The Bass Station Rack's core library (no JUCE): the CPU board running the firmware (core/board,
# core/mn1880/), the analog voice (core/voice, its netlists core/netlists/*.cir on the solver).
if(NOT TARGET lemus_bsrackcore)
  set(_bs ${BSRACK_DIR}/core)

  # The netlists, as string literals in generated/bsrack/netlists.h.
  file(GLOB BSRACK_NETLISTS CONFIGURE_DEPENDS ${_bs}/netlists/*.cir)
  set(BSRACK_NETLIST_NAMES "")
  foreach(f ${BSRACK_NETLISTS})
    get_filename_component(n ${f} NAME_WE)
    list(APPEND BSRACK_NETLIST_NAMES ${n})
  endforeach()
  set(BSRACK_GENERATED ${CMAKE_BINARY_DIR}/generated/bsrack/netlists.h)
  string(REPLACE ";" "\;" BSRACK_NAMES_ARG "${BSRACK_NETLIST_NAMES}")
  add_custom_command(OUTPUT ${BSRACK_GENERATED}
    COMMAND ${CMAKE_COMMAND} -DDIR=${_bs}/netlists -DOUT=${BSRACK_GENERATED} "-DNAMES=${BSRACK_NAMES_ARG}"
            -P ${BSRACK_DIR}/tools/cmake/netlists.cmake
    DEPENDS ${BSRACK_NETLISTS} ${BSRACK_DIR}/tools/cmake/netlists.cmake
    COMMENT "Bass Station Rack netlists")

  set(_bs_mame ${_bs}/mn1880/mn1880.cpp ${_bs}/mn1880/mn1880d.cpp)
  add_library(lemus_bsrackcore STATIC ${_bs}/machine.cpp ${_bs}/board.cpp ${_bs}/voice.cpp ${_bs}/circuit.cpp
    ${_bs}/oversampler.cpp ${_bs_mame} ${BSRACK_GENERATED})
  get_filename_component(_bs_machines ${BSRACK_DIR}/.. ABSOLUTE)
  target_include_directories(lemus_bsrackcore PUBLIC ${_bs_machines} PRIVATE ${CMAKE_BINARY_DIR}/generated/bsrack)
  target_compile_features(lemus_bsrackcore PUBLIC cxx_std_17)
  set_target_properties(lemus_bsrackcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(lemus_bsrackcore PRIVATE /O2 /W3)
    # MAME's code: its own conversions, as MAME builds it
    set_source_files_properties(${_bs_mame} PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4267;/wd4146;/wd4996")
  else()
    target_compile_options(lemus_bsrackcore PRIVATE -O3 -Wall -Wextra)
  endif()
endif()
