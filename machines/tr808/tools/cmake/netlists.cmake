# Writes the TR-808's netlists (core/netlists/*.cir) into a C++ header as string literals, so the
# voice board and the tools share one copy of each circuit.
#   cmake -DDIR=<netlists dir> -DOUT=<header> -DNAMES=bd,sd,... -P netlists.cmake
string(REPLACE "," ";" NAMES "${NAMES}")
set(text "// Generated from machines/tr808/core/netlists/*.cir by tools/cmake/netlists.cmake.\n#pragma once\n\nnamespace tr808::netlists {\n\n")
foreach(name ${NAMES})
  file(READ ${DIR}/${name}.cir body)
  string(APPEND text "inline constexpr const char *${name} = R\"cir(${body})cir\";\n\n")
endforeach()
string(APPEND text "} // namespace tr808::netlists\n")
file(WRITE ${OUT}.tmp "${text}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}.tmp ${OUT})
file(REMOVE ${OUT}.tmp)
