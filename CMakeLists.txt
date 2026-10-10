cmake_minimum_required(VERSION 3.25...3.31)

project(BIBO
	VERSION 2.0.0
	DESCRIPTION "Bear-In Bear-Out: a small 3D OpenGL game"
	LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BIBO_USE_SYSTEM_LIBS "Use the installed GLFW, GLM, Assimp, FreeType and GLEW instead of downloading them" OFF)

find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# Third-party libraries
# Downloaded and built as static libraries by default, so that only a C++17
# compiler (GCC, Clang, MinGW, MSVC, ...) and CMake are needed.
# ---------------------------------------------------------------------------
include(FetchContent)

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW) # let the options below override option() of the libraries

if(BIBO_USE_SYSTEM_LIBS)
	find_package(glfw3 3.3 REQUIRED)
	find_package(glm REQUIRED)
	find_package(assimp REQUIRED)
	find_package(Freetype REQUIRED)
	find_package(GLEW REQUIRED)
else()
	# GLFW: window, OpenGL context and input
	set(GLFW_BUILD_EXAMPLES OFF)
	set(GLFW_BUILD_TESTS OFF)
	set(GLFW_BUILD_DOCS OFF)
	set(GLFW_INSTALL OFF)
	FetchContent_Declare(glfw
		URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
		URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
		SYSTEM)

	# GLM: math (header only)
	set(GLM_BUILD_LIBRARY OFF)
	set(GLM_BUILD_TESTS OFF)
	set(GLM_BUILD_INSTALL OFF)
	FetchContent_Declare(glm
		URL https://github.com/g-truc/glm/archive/refs/tags/1.0.3.tar.gz
		URL_HASH SHA256=6775e47231a446fd086d660ecc18bcd076531cfedd912fbd66e576b118607001
		SYSTEM)

	# Assimp: model loading (only the Collada importer is needed)
	set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
	set(ASSIMP_BUILD_COLLADA_IMPORTER ON)
	set(ASSIMP_NO_EXPORT ON)
	set(ASSIMP_BUILD_TESTS OFF)
	set(ASSIMP_BUILD_ASSIMP_TOOLS OFF)
	set(ASSIMP_BUILD_SAMPLES OFF)
	set(ASSIMP_INSTALL OFF)
	set(ASSIMP_WARNINGS_AS_ERRORS OFF)
	set(ASSIMP_BUILD_ZLIB ON)
	set(ASSIMP_INJECT_DEBUG_POSTFIX OFF)
	FetchContent_Declare(assimp
		URL https://github.com/assimp/assimp/archive/refs/tags/v6.0.5.tar.gz
		URL_HASH SHA256=edf3749559c2b7d1f758ffb66fc5bec62186221e623b7f2e8969f17ee46ecb6f
		PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchAssimp.cmake
		SYSTEM)

	# FreeType: font rendering for the HUD
	set(FT_DISABLE_ZLIB ON)
	set(FT_DISABLE_BZIP2 ON)
	set(FT_DISABLE_PNG ON)
	set(FT_DISABLE_HARFBUZZ ON)
	set(FT_DISABLE_BROTLI ON)
	set(SKIP_INSTALL_ALL ON) # cmake --install only installs the game
	FetchContent_Declare(freetype
		URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-3.tar.gz
		URL_HASH SHA256=dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261
		SYSTEM)

	FetchContent_MakeAvailable(glfw glm assimp freetype)
	if(NOT TARGET Freetype::Freetype)
		add_library(Freetype::Freetype ALIAS freetype)
	endif()

	# GLEW: OpenGL function loader (its own CMake project is not needed, glew.c is compiled directly)
	FetchContent_Populate(glew
		URL https://github.com/nigels-com/glew/releases/download/glew-2.3.1/glew-2.3.1.tgz
		URL_HASH SHA256=b64790f94b926acd7e8f84c5d6000a86cb43967bd1e688b03089079799c9e889
		SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/glew-src)
	add_library(bibo_glew STATIC ${glew_SOURCE_DIR}/src/glew.c)
	target_include_directories(bibo_glew SYSTEM PUBLIC ${glew_SOURCE_DIR}/include)
	target_compile_definitions(bibo_glew PUBLIC GLEW_STATIC GLEW_NO_GLU)
	target_link_libraries(bibo_glew PUBLIC OpenGL::GL)
	add_library(GLEW::GLEW ALIAS bibo_glew)
endif()

# stb_image: image loading (header only, replaces DevIL)
FetchContent_Populate(stb
	URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
	URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
	SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/stb-src)
add_library(bibo_stb INTERFACE)
target_include_directories(bibo_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

# Jolt Physics: rigid bodies and character controller (replaces Nvidia PhysX 3.3)
set(TARGET_UNIT_TESTS OFF)
set(TARGET_HELLO_WORLD OFF)
set(TARGET_PERFORMANCE_TEST OFF)
set(TARGET_SAMPLES OFF)
set(TARGET_VIEWER OFF)
set(ENABLE_ALL_WARNINGS OFF)
set(ENABLE_INSTALL OFF)
set(INTERPROCEDURAL_OPTIMIZATION OFF)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)
set(CPP_RTTI_ENABLED ON) # the game code is compiled with RTTI and derives from Jolt classes
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
set(OVERRIDE_CXX_FLAGS OFF)
set(JPH_USE_DX12 OFF) # GPU compute (hair simulation) is not used
set(JPH_USE_VK OFF)
set(JPH_USE_MTL OFF)
set(JPH_USE_CPU_COMPUTE OFF)
FetchContent_Declare(JoltPhysics
	URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.6.0.tar.gz
	URL_HASH SHA256=6e069ee0172478cc78182047aac87e5310ba14a67a53348ae14cc37801fd3f8e
	SOURCE_SUBDIR Build
	SYSTEM)
FetchContent_MakeAvailable(JoltPhysics)

# ---------------------------------------------------------------------------
# Game
# ---------------------------------------------------------------------------
add_executable(BIBO
	BIBO/Main.cpp
	BIBO/Physics/Physics.cpp
	BIBO/Physics/Physics.hpp
	BIBO/Resources/AABB.cpp
	BIBO/Resources/AABB.hpp
	BIBO/Resources/Material.cpp
	BIBO/Resources/Material.hpp
	BIBO/Resources/Mesh.cpp
	BIBO/Resources/Mesh.hpp
	BIBO/Resources/Model.cpp
	BIBO/Resources/Model.hpp
	BIBO/Resources/Plane.cpp
	BIBO/Resources/Plane.hpp
	BIBO/Resources/Shader.cpp
	BIBO/Resources/Shader.hpp
	BIBO/Resources/Texture.cpp
	BIBO/Resources/Texture.hpp
	BIBO/Scene/Bear.cpp
	BIBO/Scene/Bear.hpp
	BIBO/Scene/Camera.cpp
	BIBO/Scene/Camera.hpp
	BIBO/Scene/Cube.cpp
	BIBO/Scene/Cube.hpp
	BIBO/Scene/Geometry.cpp
	BIBO/Scene/Geometry.hpp
	BIBO/Scene/Light.cpp
	BIBO/Scene/Light.hpp
	BIBO/Scene/Scene.cpp
	BIBO/Scene/Scene.hpp
	BIBO/Scene/SceneObject.cpp
	BIBO/Scene/SceneObject.hpp
	BIBO/Scene/ViewFrustum.cpp
	BIBO/Scene/ViewFrustum.hpp
	BIBO/Util/Console.hpp
	BIBO/Util/Util.cpp
	BIBO/Util/Util.hpp)

target_include_directories(BIBO PRIVATE BIBO)
target_compile_definitions(BIBO PRIVATE
	GLM_FORCE_CTOR_INIT     # GLM >= 0.9.9 no longer initializes vectors/matrices by default (0.9.7 did)
	GLM_ENABLE_EXPERIMENTAL # glm/gtx/rotate_vector.hpp
	GLEW_NO_GLU)
target_link_libraries(BIBO PRIVATE
	OpenGL::GL
	GLEW::GLEW
	glfw
	glm::glm
	assimp::assimp
	Freetype::Freetype
	Jolt
	bibo_stb)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(BIBO PRIVATE -Wall)
endif()
if(MINGW AND NOT BIBO_USE_SYSTEM_LIBS)
	# Self-contained executable without MinGW runtime DLLs
	target_link_options(BIBO PRIVATE -static)
endif()
if(MSVC)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT BIBO)
	set_property(TARGET BIBO PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:BIBO>)
endif()

# The game loads its assets relative to the working directory, so they are copied next to the executable
set(BIBO_ASSET_DIRS Font Model Shader Texture)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
	set(BIBO_COPY_DIRECTORY copy_directory_if_different)
else()
	set(BIBO_COPY_DIRECTORY copy_directory)
endif()
foreach(asset_dir IN LISTS BIBO_ASSET_DIRS)
	add_custom_command(TARGET BIBO POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E ${BIBO_COPY_DIRECTORY}
			${CMAKE_CURRENT_SOURCE_DIR}/${asset_dir} $<TARGET_FILE_DIR:BIBO>/${asset_dir}
		COMMENT "Copying ${asset_dir}/ next to the executable"
		VERBATIM)
endforeach()

install(TARGETS BIBO RUNTIME DESTINATION .)
install(DIRECTORY ${BIBO_ASSET_DIRS} DESTINATION .)
