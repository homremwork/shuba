cmake_minimum_required(VERSION 3.22)

project(Shuba LANGUAGES C CXX)

option(SHUBA_BUILD_HEADLESS_TESTS "Build Linux/headless core test targets." ON)
option(SHUBA_ENABLE_CPPCHECK
	"Run cppcheck alongside supported C++ target builds when cppcheck is available."
	OFF)
option(SHUBA_ENABLE_CLANG_TIDY
	"Run clang-tidy alongside supported C++ target builds when clang-tidy is available."
	OFF)
option(SHUBA_ENABLE_ASAN
	"Enable AddressSanitizer for Linux Debug host targets." OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SHUBA_CPPCHECK_CHECKS "warning,performance,portability"
	CACHE STRING "Cppcheck check groups used by Shuba static-analysis targets.")
set(SHUBA_CLANG_TIDY_CHECKS
	"clang-analyzer-*,bugprone-*,performance-*,portability-*,-bugprone-easily-swappable-parameters,-bugprone-switch-missing-default-case,-bugprone-exception-escape,-bugprone-unchecked-optional-access,-bugprone-narrowing-conversions,-portability-avoid-pragma-once"
	CACHE STRING "clang-tidy checks used by Shuba static-analysis targets.")
set(SHUBA_CLANG_TIDY_WARNINGS_AS_ERRORS "*"
	CACHE STRING "clang-tidy warnings-as-errors filter for Shuba analysis targets.")

find_program(SHUBA_CPPCHECK_EXECUTABLE NAMES cppcheck)
find_program(SHUBA_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
find_program(SHUBA_RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy run-clang-tidy.py)

set(SHUBA_CPPCHECK_COMMON_ARGS
	"--enable=${SHUBA_CPPCHECK_CHECKS}"
	"--error-exitcode=1"
	"--inline-suppr"
	"--suppress=missingIncludeSystem"
	"--suppress=preprocessorErrorDirective:third_party/JUCE/modules/juce_core/system/juce_TargetPlatform.h:224"
	"--suppress=syntaxError:third_party/glaze/include/glaze/tuplet/tuple.hpp:16")
set(SHUBA_ANALYZER_SOURCE_FILTER "^${CMAKE_SOURCE_DIR}/(Source|tests)/")
set(SHUBA_ANALYZER_FILE_REGEX "^${CMAKE_SOURCE_DIR}/(Source|tests)/.*\\.cpp$")

function(shuba_apply_common_target_options target_name)
	set_target_properties(${target_name} PROPERTIES CXX_EXTENSIONS OFF)

	if(SHUBA_ENABLE_CPPCHECK)
		if(SHUBA_CPPCHECK_EXECUTABLE)
			set_target_properties(${target_name} PROPERTIES
				CXX_CPPCHECK "${SHUBA_CPPCHECK_EXECUTABLE};${SHUBA_CPPCHECK_COMMON_ARGS}")
		else()
			message(WARNING
				"SHUBA_ENABLE_CPPCHECK is ON, but cppcheck was not found; ${target_name} will build without cppcheck.")
		endif()
	endif()

	if(SHUBA_ENABLE_CLANG_TIDY)
		if(SHUBA_CLANG_TIDY_EXECUTABLE)
			set_target_properties(${target_name} PROPERTIES
				CXX_CLANG_TIDY "${SHUBA_CLANG_TIDY_EXECUTABLE};-checks=${SHUBA_CLANG_TIDY_CHECKS};-header-filter=${SHUBA_ANALYZER_SOURCE_FILTER};-warnings-as-errors=${SHUBA_CLANG_TIDY_WARNINGS_AS_ERRORS}")
		else()
			message(WARNING
				"SHUBA_ENABLE_CLANG_TIDY is ON, but clang-tidy was not found; ${target_name} will build without clang-tidy.")
		endif()
	endif()

	if(SHUBA_ENABLE_ASAN)
		if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
			message(FATAL_ERROR "SHUBA_ENABLE_ASAN is supported only for Linux host builds.")
		endif()

		if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
			message(WARNING
				"SHUBA_ENABLE_ASAN adds sanitizer flags only to Debug builds; configure with -DCMAKE_BUILD_TYPE=Debug for the Linux ASAN run.")
		endif()

		if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
			message(FATAL_ERROR
				"SHUBA_ENABLE_ASAN requires a Clang or GNU C++ compiler for the Linux host build.")
		endif()

		target_compile_options(${target_name} PRIVATE
			$<$<CONFIG:Debug>:-fsanitize=address>
			$<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
		target_link_options(${target_name} PRIVATE
			$<$<CONFIG:Debug>:-fsanitize=address>)
	endif()
endfunction()

add_library(shuba_core
	Source/Catalog/BackupArchive.cpp
	Source/Catalog/CatalogReplacement.cpp
	Source/Catalog/CatalogRepository.cpp
	Source/Catalog/PhotoExport.cpp
	Source/Catalog/PhotoImport.cpp
	Source/Catalog/Search.cpp
	Source/Core/Clock.cpp
	Source/Core/Identifier.cpp
	Source/Core/OperationGate.cpp
	Source/Core/Result.cpp
	Source/Domain/Domain.cpp
	Source/Platform/LinuxFakes.cpp
	Source/Platform/PlatformServices.cpp
	Source/Persistence/CatalogStorage.cpp
	Source/Persistence/JsonlCatalog.cpp
	Source/Persistence/MetadataSchema.cpp
	Source/UI/CatalogSession.cpp)
target_compile_features(shuba_core PUBLIC cxx_std_23)
target_include_directories(shuba_core
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glaze/include")
shuba_apply_common_target_options(shuba_core)

set(JUCE_MODULES_ONLY ON CACHE BOOL "Only configure JUCE modules for Shuba host builds." FORCE)
add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)

add_library(shuba_juce_platform STATIC
	Source/Platform/JuceJpegExport.cpp
	Source/Platform/JuceZipArchive.cpp)
target_compile_features(shuba_juce_platform PUBLIC cxx_std_23)
target_include_directories(shuba_juce_platform
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/JuceLibraryCode")
target_compile_definitions(shuba_juce_platform PUBLIC
	JUCE_USE_CURL=0
	JUCE_WEB_BROWSER=0
	JUCE_USE_CAMERA=0
	JUCE_USE_MP3AUDIOFORMAT=0)
target_link_libraries(shuba_juce_platform
	PUBLIC shuba_core
	PRIVATE juce::juce_graphics)
shuba_apply_common_target_options(shuba_juce_platform)

if(SHUBA_BUILD_HEADLESS_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

if(SHUBA_CPPCHECK_EXECUTABLE)
	add_custom_target(shuba_cppcheck
		COMMAND "${CMAKE_COMMAND}" -E make_directory
			"${CMAKE_BINARY_DIR}/cppcheck"
		COMMAND "${SHUBA_CPPCHECK_EXECUTABLE}"
			"--project=${CMAKE_BINARY_DIR}/compile_commands.json"
			${SHUBA_CPPCHECK_COMMON_ARGS}
			"--cppcheck-build-dir=${CMAKE_BINARY_DIR}/cppcheck"
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		COMMENT "Running cppcheck over Shuba host C++ targets"
		VERBATIM
		COMMAND_EXPAND_LISTS)
else()
	add_custom_target(shuba_cppcheck
		COMMAND "${CMAKE_COMMAND}" -E echo
			"cppcheck was not found; shuba_cppcheck is skipped."
		VERBATIM)
endif()

if(SHUBA_CLANG_TIDY_EXECUTABLE AND SHUBA_RUN_CLANG_TIDY_EXECUTABLE)
	add_custom_target(shuba_clang_tidy
		COMMAND "${SHUBA_RUN_CLANG_TIDY_EXECUTABLE}"
			-p "${CMAKE_BINARY_DIR}"
			-clang-tidy-binary "${SHUBA_CLANG_TIDY_EXECUTABLE}"
			-checks=${SHUBA_CLANG_TIDY_CHECKS}
			-header-filter=${SHUBA_ANALYZER_SOURCE_FILTER}
			-source-filter=${SHUBA_ANALYZER_SOURCE_FILTER}
			-warnings-as-errors=${SHUBA_CLANG_TIDY_WARNINGS_AS_ERRORS}
			${SHUBA_ANALYZER_FILE_REGEX}
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		COMMENT "Running clang-tidy over Shuba host C++ targets"
		VERBATIM)
elseif(SHUBA_CLANG_TIDY_EXECUTABLE)
	add_custom_target(shuba_clang_tidy
		COMMAND "${CMAKE_COMMAND}" -E echo
			"run-clang-tidy was not found; use -DSHUBA_ENABLE_CLANG_TIDY=ON to run clang-tidy during target builds."
		VERBATIM)
else()
	add_custom_target(shuba_clang_tidy
		COMMAND "${CMAKE_COMMAND}" -E echo
			"clang-tidy was not found; shuba_clang_tidy is skipped."
		VERBATIM)
endif()

add_custom_target(shuba_static_checks
	DEPENDS shuba_cppcheck shuba_clang_tidy)
