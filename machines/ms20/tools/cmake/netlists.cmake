# Writes the MS-20's netlists (core/netlists/*.cir) into a C++ header as string literals, so the
# voice board and the tools share one copy of each circuit.
#   cmake -DDIR=<netlists dir> -DOUT=<header> -DNAMES=bd,sd,... -P netlists.cmake
set(text "// Generated from machines/ms20/core/netlists/*.cir by tools/cmake/netlists.cmake.\n#pragma once\n\n#include <cstring>\n\nnamespace ms20::netlists {\n\n")
# (a file name's characters other than letters, digits and _ become _ in its identifier:
# models-dynamic.cir is models_dynamic)
string(REPLACE "," ";" NAMES "${NAMES}")
foreach(name ${NAMES})
  file(READ ${DIR}/${name}.cir body)
  string(MAKE_C_IDENTIFIER ${name} id)
  string(APPEND text "inline constexpr const char *${id} = R\"cir(${body})cir\";\n\n")
endforeach()
# and every netlist by name, for the blocks the voicing board loads by name (null: none)
string(APPEND text "inline const char *find(const char *name)\n{\n\tstruct Entry {\n\t\tconst char *name, *text;\n\t};\n\tstatic const Entry all[] = {\n")
foreach(name ${NAMES})
  string(MAKE_C_IDENTIFIER ${name} id)
  string(APPEND text "\t\t{\"${name}\", ${id}},\n")
endforeach()
string(APPEND text "\t};\n\tfor (const auto &e : all)\n\t\tif (std::strcmp(e.name, name) == 0)\n\t\t\treturn e.text;\n\treturn nullptr;\n}\n\n} // namespace ms20::netlists\n")
file(WRITE ${OUT}.tmp "${text}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}.tmp ${OUT})
file(REMOVE ${OUT}.tmp)
