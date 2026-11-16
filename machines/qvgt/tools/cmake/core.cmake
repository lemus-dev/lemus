# The QuadraVerb GT's core library (no JUCE): the 80C31, the board, the DASP 24, the analog stages
# (the circuit solver, machines/tr909's, with the netlists in core/netlists/), the unit.
if(NOT TARGET lemus_qvgtcore)
  set(_qg ${QVGT_DIR}/core)
  # the netlists, as string literals in generated/qvgt/netlists.h
  file(GLOB _qg_netlists CONFIGURE_DEPENDS ${_qg}/netlists/*.cir)
  set(_qg_names "")
  foreach(f ${_qg_netlists})
    get_filename_component(n ${f} NAME_WE)
    list(APPEND _qg_names ${n})
  endforeach()
  set(QVGT_GENERATED ${CMAKE_BINARY_DIR}/generated/qvgt/netlists.h)
  string(REPLACE ";" "," _qg_names_arg "${_qg_names}")
  add_custom_command(OUTPUT ${QVGT_GENERATED}
    COMMAND ${CMAKE_COMMAND} -DDIR=${_qg}/netlists -DOUT=${QVGT_GENERATED} "-DNAMES=${_qg_names_arg}"
            -P ${QVGT_DIR}/tools/cmake/netlists.cmake
    DEPENDS ${_qg_netlists} ${QVGT_DIR}/tools/cmake/netlists.cmake
    COMMENT "QuadraVerb GT netlists")
  add_library(lemus_qvgtcore STATIC ${_qg}/i8031.cpp ${_qg}/board.cpp ${_qg}/dasp24.cpp ${_qg}/circuit.cpp ${_qg}/preamp.cpp ${_qg}/unit.cpp
    ${QVGT_GENERATED})
  get_filename_component(_qg_machines ${QVGT_DIR}/.. ABSOLUTE)
  target_include_directories(lemus_qvgtcore PUBLIC ${_qg_machines} ${CMAKE_BINARY_DIR}/generated)
  target_compile_features(lemus_qvgtcore PUBLIC cxx_std_17)
  set_target_properties(lemus_qvgtcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(lemus_qvgtcore PRIVATE /O2 /W3)
  else()
    target_compile_options(lemus_qvgtcore PRIVATE -O3 -Wall -Wextra)
  endif()
endif()
