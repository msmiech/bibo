# Applied to the downloaded Assimp sources (runs in the Assimp source directory).
#
# The poly2tri library bundled with Assimp 6.0.5 uses std::ostream in shapes.h without including <ostream>.
# Recent standard libraries (e.g. libc++ of LLVM 20+) no longer include it transitively, which breaks the build.
set(header "contrib/poly2tri/poly2tri/common/shapes.h")
file(READ "${header}" content)
if(NOT content MATCHES "#include <ostream>")
	string(REPLACE "#include <cstddef>" "#include <cstddef>\n#include <ostream>" content "${content}")
	file(WRITE "${header}" "${content}")
endif()
