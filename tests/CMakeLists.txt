# Catch2: test framework (https://github.com/catchorg/Catch2)
FetchContent_Declare(Catch2
	URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.0.tar.gz
	URL_HASH SHA256=0957cae5821b17ce07f0833aaa52b5137643a8382203221f363a8303c109af34
	SYSTEM)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(bibo_tests
	TestSupport.cpp
	TestSupport.hpp
	# Game logic, no assets or graphics card needed
	unit/AnimalsTest.cpp
	unit/AnimationTest.cpp
	unit/CharacterControllerTest.cpp
	unit/DemoScriptTest.cpp
	unit/FoliageTest.cpp
	unit/FurTest.cpp
	unit/GameRulesTest.cpp
	unit/InputTest.cpp
	unit/MathTest.cpp
	unit/MusicTest.cpp
	unit/PhysicsWorldTest.cpp
	unit/QuestTest.cpp
	unit/RagdollTest.cpp
	unit/SalmonQuestTest.cpp
	unit/StonesQuestTest.cpp
	unit/TextureGeneratorTest.cpp
	unit/ViewFrustumTest.cpp
	unit/WorldUnitTest.cpp
	# Content of the asset files, no graphics card needed
	assets/AnimalsTrackTest.cpp
	assets/AssetFilesTest.cpp
	assets/BearCharacterTest.cpp
	assets/BearModelTest.cpp
	assets/TrackAssetTest.cpp
	assets/TrackPhysicsTest.cpp
	assets/WorldTest.cpp
	# Need an OpenGL 4.3 context, skipped if there is none (e.g. no display)
	gl/CameraTest.cpp
	gl/ModelTest.cpp
	gl/SceneTest.cpp
	gl/ShaderTest.cpp
	gl/TextureTest.cpp
	gl/WorldRenderTest.cpp)

target_include_directories(bibo_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bibo_tests PRIVATE bibo_core Catch2::Catch2WithMain)
# The tests read the assets directly from the source tree
target_compile_definitions(bibo_tests PRIVATE BIBO_ASSET_DIR="${PROJECT_SOURCE_DIR}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(bibo_tests PRIVATE -Wall)
endif()

# Every test case is a separate CTest test; the Catch2 tags (e.g. [physics], [gl]) become CTest labels.
# Tests that are skipped (e.g. no OpenGL 4.3) are reported as skipped, not as passed or failed.
catch_discover_tests(bibo_tests
	DISCOVERY_MODE PRE_TEST
	ADD_TAGS_AS_LABELS)
