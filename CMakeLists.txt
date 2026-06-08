cmake_minimum_required(VERSION 3.22)

project(Shuba LANGUAGES CXX)

option(SHUBA_BUILD_HEADLESS_TESTS "Build Linux/headless core test targets." ON)

add_library(shuba_core
	Source/Core/Clock.cpp
	Source/Core/Identifier.cpp
	Source/Core/OperationGate.cpp
	Source/Core/Result.cpp
	Source/Domain/Domain.cpp
	Source/Persistence/JsonlCatalog.cpp
	Source/Persistence/MetadataSchema.cpp)
target_compile_features(shuba_core PUBLIC cxx_std_23)
target_include_directories(shuba_core
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glaze/include")
set_target_properties(shuba_core PROPERTIES CXX_EXTENSIONS OFF)

if(SHUBA_BUILD_HEADLESS_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
