# The Xpander's core library (no JUCE): the two 6809 boards, the analog voices, the machine.
if(NOT TARGET lemus_xpandercore)
  set(_xp ${XPANDER_DIR}/core)
  add_library(lemus_xpandercore STATIC ${_xp}/m6809/m6809.cpp ${_xp}/pit8253.cpp ${_xp}/voiceboard.cpp ${_xp}/mainboard.cpp
    ${_xp}/blep.cpp ${_xp}/cem3374.cpp ${_xp}/cem3372.cpp ${_xp}/voice.cpp ${_xp}/decimator.cpp ${_xp}/machine.cpp
    ${_xp}/warm.cpp)
  get_filename_component(_xp_machines ${XPANDER_DIR}/.. ABSOLUTE)
  target_include_directories(lemus_xpandercore PUBLIC ${_xp_machines})
  target_compile_features(lemus_xpandercore PUBLIC cxx_std_17)
  set_target_properties(lemus_xpandercore PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(lemus_xpandercore PRIVATE /O2 /W3)
  else()
    target_compile_options(lemus_xpandercore PRIVATE -O3 -Wall -Wextra)
  endif()
endif()
