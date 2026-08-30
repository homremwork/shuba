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
option(SHUBA_ENABLE_REAL_JPEG_XL_CODEC_TESTS
	"Build focused tests for the real JPEG XL codec using system libjxl." ON)
option(SHUBA_CI_STRICT_TOOLS
	"Fail configuration when tools required by the selected CI lane are missing." OFF)
set(SHUBA_CI_HOST_LIBJXL_PREFIX "" CACHE PATH
	"Optional validated host-libjxl installation prefix used by CI.")

find_program(SHUBA_MSGFMT_EXECUTABLE NAMES msgfmt REQUIRED)
find_program(SHUBA_MSGCMP_EXECUTABLE NAMES msgcmp REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SHUBA_CPPCHECK_CHECKS "warning,performance,portability"
	CACHE STRING "Cppcheck check groups used by Shuba static-analysis targets.")
set(SHUBA_CLANG_TIDY_CHECKS
	"clang-analyzer-*,bugprone-*,performance-*,portability-*,-bugprone-easily-swappable-parameters,-bugprone-switch-missing-default-case,-bugprone-exception-escape,-bugprone-unchecked-optional-access,-bugprone-narrowing-conversions,-bugprone-chained-comparison,-portability-avoid-pragma-once,-performance-enum-size,-performance-unnecessary-value-param,-performance-move-const-arg,-clang-analyzer-optin.core.EnumCastOutOfRange"
	CACHE STRING "clang-tidy checks used by Shuba static-analysis targets.")
set(SHUBA_CLANG_TIDY_WARNINGS_AS_ERRORS "*"
	CACHE STRING "clang-tidy warnings-as-errors filter for Shuba analysis targets.")
set(SHUBA_CLANG_TIDY_EXTRA_ARGS ""
	CACHE STRING "Extra clang-tidy driver arguments for toolchain compatibility.")

find_program(SHUBA_CPPCHECK_EXECUTABLE NAMES cppcheck)
find_program(SHUBA_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
find_program(SHUBA_RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy run-clang-tidy.py)

if(SHUBA_CI_STRICT_TOOLS)
	if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
		message(FATAL_ERROR "SHUBA_CI_STRICT_TOOLS is supported only for Linux CI builds.")
	endif()
	if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		message(FATAL_ERROR "Shuba CI host presets require the GNU C++ compiler.")
	endif()
	if(SHUBA_ENABLE_CPPCHECK AND NOT SHUBA_CPPCHECK_EXECUTABLE)
		message(FATAL_ERROR "SHUBA_ENABLE_CPPCHECK requires cppcheck in strict CI mode.")
	endif()
	if(SHUBA_ENABLE_CLANG_TIDY AND NOT SHUBA_CLANG_TIDY_EXECUTABLE)
		message(FATAL_ERROR "SHUBA_ENABLE_CLANG_TIDY requires clang-tidy in strict CI mode.")
	endif()
	if(SHUBA_ENABLE_CLANG_TIDY AND NOT SHUBA_RUN_CLANG_TIDY_EXECUTABLE)
		message(FATAL_ERROR "SHUBA_ENABLE_CLANG_TIDY requires run-clang-tidy in strict CI mode.")
	endif()
endif()

set(SHUBA_CPPCHECK_COMMON_ARGS
	"--enable=${SHUBA_CPPCHECK_CHECKS}"
	"--error-exitcode=1"
	"--inline-suppr"
	"--suppress=missingIncludeSystem"
	"--suppress=preprocessorErrorDirective:*/third_party/JUCE/modules/juce_core/system/juce_TargetPlatform.h:224"
	"--suppress=preprocessorErrorDirective:*/third_party/glaze/include/glaze/util/fast_float.hpp:258"
	"--suppress=syntaxError:third_party/glaze/include/glaze/tuplet/tuple.hpp:16"
	"--suppress=syntaxError:*/tests/B27LocalizationLanguageTests.cpp:23"
	"--suppress=syntaxError:*/tests/C30LocalizationPresentationLayoutTests.cpp:40"
	"--suppress=passedByValue:*/Source/Localization/CatalogDefinition.cpp:65"
	"--suppress=uninitMemberVarNoCtor:*/Source/Localization/PhotoWorkflowLocalization.cpp:13"
	"--suppress=uninitMemberVarNoCtor:*/Source/UI/AppShell/PhotoCoordinator.hpp:48"
	"--suppress=uninitMemberVarNoCtor:*/Source/Localization/MessageCatalog.hpp:16"
	"--suppress=uninitMemberVarNoCtor:*/Source/Localization/Types.hpp:121")
set(SHUBA_ANALYZER_SOURCE_FILTER "^${CMAKE_SOURCE_DIR}/(Source|tests)/")
set(SHUBA_ANALYZER_FILE_REGEX "^${CMAKE_SOURCE_DIR}/(Source|tests)/.*\\.cpp$")

function(shuba_apply_common_target_options target_name)
	set_target_properties(${target_name} PROPERTIES CXX_EXTENSIONS OFF)

	if(SHUBA_ENABLE_CPPCHECK)
		if(SHUBA_CPPCHECK_EXECUTABLE)
			if(NOT SHUBA_CI_STRICT_TOOLS)
				set_target_properties(${target_name} PROPERTIES
					CXX_CPPCHECK "${SHUBA_CPPCHECK_EXECUTABLE};${SHUBA_CPPCHECK_COMMON_ARGS}")
			endif()
		else()
			message(WARNING
				"SHUBA_ENABLE_CPPCHECK is ON, but cppcheck was not found; ${target_name} will build without cppcheck.")
		endif()
	endif()

	if(SHUBA_ENABLE_CLANG_TIDY)
		if(SHUBA_CLANG_TIDY_EXECUTABLE)
			if(NOT SHUBA_CI_STRICT_TOOLS)
				set_target_properties(${target_name} PROPERTIES
					CXX_CLANG_TIDY "${SHUBA_CLANG_TIDY_EXECUTABLE};-checks=${SHUBA_CLANG_TIDY_CHECKS};-header-filter=${SHUBA_ANALYZER_SOURCE_FILTER};-warnings-as-errors=${SHUBA_CLANG_TIDY_WARNINGS_AS_ERRORS}")
			endif()
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

# Keep source-owned message definitions and production PO acceptance independent
# of generated BinaryData so the catalog contract can validate tracked inputs
# before resource generation.
add_library(shuba_localization_catalog STATIC
	Source/Localization/Catalog.cpp
	Source/Localization/CatalogDefinition.cpp
	Source/Localization/MessageCatalog.cpp
	Source/Localization/PhotoWorkflowLocalization.cpp)
target_compile_features(shuba_localization_catalog PUBLIC cxx_std_23)
target_include_directories(shuba_localization_catalog
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spiritless_po/include")
shuba_apply_common_target_options(shuba_localization_catalog)

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
	Source/Localization/Language.cpp
	Source/Platform/AndroidPreviousExit.cpp
	Source/Platform/LinuxFakes.cpp
	Source/Platform/PlatformServices.cpp
	Source/Persistence/CatalogStorage.cpp
	Source/Persistence/JsonlCatalog.cpp
	Source/Persistence/MetadataSchema.cpp
	Source/UI/Session/CatalogStartupSession.cpp
	Source/UI/Session/AndroidPreviousExitArtifacts.cpp
	Source/UI/Session/StartupRecoverySession.cpp
	Source/UI/Session/EntityEditSession.cpp
	Source/UI/Session/ImagePreviewSession.cpp
	Source/UI/Session/PhotoSession.cpp
	Source/UI/Session/BackupRecoverySession.cpp
	Source/UI/CatalogSession.cpp)
target_compile_features(shuba_core PUBLIC cxx_std_23)
target_include_directories(shuba_core
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glaze/include")
shuba_apply_common_target_options(shuba_core)

if(SHUBA_ENABLE_REAL_JPEG_XL_CODEC_TESTS)
	if(SHUBA_CI_HOST_LIBJXL_PREFIX)
		cmake_path(ABSOLUTE_PATH SHUBA_CI_HOST_LIBJXL_PREFIX
			NORMALIZE OUTPUT_VARIABLE SHUBA_CI_HOST_LIBJXL_PREFIX_ABSOLUTE)
		if(NOT IS_DIRECTORY "${SHUBA_CI_HOST_LIBJXL_PREFIX_ABSOLUTE}")
			message(FATAL_ERROR "SHUBA_CI_HOST_LIBJXL_PREFIX is not a directory.")
		endif()
		set(SHUBA_CI_HOST_LIBJXL_PKGCONFIG
			"${SHUBA_CI_HOST_LIBJXL_PREFIX_ABSOLUTE}/lib/pkgconfig")
		foreach(SHUBA_CI_REQUIRED_PC IN ITEMS libjxl.pc libjxl_threads.pc)
			if(NOT EXISTS "${SHUBA_CI_HOST_LIBJXL_PKGCONFIG}/${SHUBA_CI_REQUIRED_PC}")
				message(FATAL_ERROR
					"Validated host-libjxl prefix lacks ${SHUBA_CI_REQUIRED_PC}.")
			endif()
		endforeach()
		set(SHUBA_CI_PKG_CONFIG_ARGN_DEFINED false)
		if(DEFINED PKG_CONFIG_ARGN)
			set(SHUBA_CI_PKG_CONFIG_ARGN_DEFINED true)
			set(SHUBA_CI_SAVED_PKG_CONFIG_ARGN "${PKG_CONFIG_ARGN}")
		endif()
		set(ENV{PKG_CONFIG_PATH} "${SHUBA_CI_HOST_LIBJXL_PKGCONFIG}")
		set(PKG_CONFIG_ARGN "--static")
	endif()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(SHUBA_LIBJXL IMPORTED_TARGET REQUIRED libjxl>=0.12.0)
	pkg_check_modules(SHUBA_LIBJXL_THREADS IMPORTED_TARGET REQUIRED
		libjxl_threads>=0.12.0)
	if(SHUBA_CI_STRICT_TOOLS AND SHUBA_CI_HOST_LIBJXL_PREFIX AND
		NOT SHUBA_LIBJXL_PREFIX STREQUAL SHUBA_CI_HOST_LIBJXL_PREFIX_ABSOLUTE)
		message(FATAL_ERROR "pkg-config selected libjxl outside SHUBA_CI_HOST_LIBJXL_PREFIX.")
	endif()
	if(SHUBA_CI_HOST_LIBJXL_PREFIX)
		if(SHUBA_CI_PKG_CONFIG_ARGN_DEFINED)
			set(PKG_CONFIG_ARGN "${SHUBA_CI_SAVED_PKG_CONFIG_ARGN}")
		else()
			unset(PKG_CONFIG_ARGN)
		endif()
	endif()

	add_library(shuba_jpeg_xl_codec STATIC
		Source/Platform/JpegXlPhotoCodec.cpp)
	target_compile_features(shuba_jpeg_xl_codec PUBLIC cxx_std_23)
	target_include_directories(shuba_jpeg_xl_codec
		PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
	target_link_libraries(shuba_jpeg_xl_codec
		PUBLIC shuba_core PkgConfig::SHUBA_LIBJXL
			PkgConfig::SHUBA_LIBJXL_THREADS)
	shuba_apply_common_target_options(shuba_jpeg_xl_codec)
endif()

# BinaryData is generated by JUCE's juceaide helper. Keep the full JUCE CMake
# integration available so the authoritative tracked PO resource can be embedded;
# JUCE_MODULES_ONLY intentionally omits that helper.
set(JUCE_MODULES_ONLY OFF CACHE BOOL "Only configure the JUCE modules." FORCE)
add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)

juce_add_binary_data(shuba_localization_resources
	NAMESPACE ShubaBinaryData
	HEADER_NAME BinaryData.h
	SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Localization/ru.po")
target_compile_features(shuba_localization_resources PRIVATE cxx_std_23)
shuba_apply_common_target_options(shuba_localization_resources)

target_sources(shuba_core PRIVATE
	Source/Localization/EmbeddedCatalog.cpp
	Source/Localization/Facade.cpp
	Source/Localization/Formatting.cpp)
target_link_libraries(shuba_core PRIVATE
	shuba_localization_catalog shuba_localization_resources)

# The generated BinaryData header is an implementation detail of the catalog
# facade, but standalone clang-tidy reads from compile_commands.json and must
# receive the same generated include path as normal target compilation.
target_include_directories(shuba_core PRIVATE
	"$<TARGET_PROPERTY:shuba_localization_resources,BINARY_DIR>")

add_library(shuba_juce_platform STATIC
	Source/Platform/JuceHashing.cpp
	Source/Platform/JuceAndroidPreviousExit.cpp
	Source/Platform/JuceJpegExport.cpp
	Source/Platform/JuceZipArchive.cpp
	Source/UI/AppShell/OperationRunner.cpp
	Source/UI/AppShell/PreviewScheduler.cpp
	Source/UI/AppShell/EditCoordinator.cpp
	Source/UI/AppShell/PhotoCoordinator.cpp
	Source/UI/AppShell/RouteCoordinator.cpp
	Source/UI/AppShell/Component.cpp
	Source/UI/AppShell/ScreenRenderer.cpp
	Source/UI/Screens/CatalogScreens.cpp
	Source/UI/Screens/FormAndPhotoScreens.cpp
	Source/UI/Screens/MaintenanceScreens.cpp
	Source/UI/Screens/StorageScreens.cpp
	Source/UI/AppShell/ChromeComponent.cpp
	Source/UI/AppShell/ContentComponent.cpp
	Source/UI/View/Primitives/Palette.cpp
	Source/UI/View/Primitives/TouchGuards.cpp
	Source/UI/View/Primitives/PhotoViewerPinchGesture.cpp
	Source/UI/View/Primitives/Rows.cpp
	Source/UI/View/Primitives/Previews.cpp
	Source/UI/View/Primitives/Forms.cpp
	Source/UI/View/Primitives/PhotoManagement.cpp
	Source/UI/View/ScreenText.cpp)
target_compile_features(shuba_juce_platform PUBLIC cxx_std_23)
target_include_directories(shuba_juce_platform
	PUBLIC
		"${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_definitions(shuba_juce_platform PUBLIC
	JUCE_USE_CURL=0
	JUCE_WEB_BROWSER=0
	JUCE_USE_CAMERA=0
	JUCE_USE_MP3AUDIOFORMAT=0)
target_link_libraries(shuba_juce_platform
	PUBLIC shuba_core juce::juce_graphics juce::juce_gui_basics
		juce::juce_cryptography)
shuba_apply_common_target_options(shuba_juce_platform)

add_executable(shuba_localization_contract
	tests/LocalizationCatalogContract.cpp)
target_compile_features(shuba_localization_contract PRIVATE cxx_std_23)
target_link_libraries(shuba_localization_contract PRIVATE shuba_localization_catalog)
shuba_apply_common_target_options(shuba_localization_contract)

if(SHUBA_BUILD_HEADLESS_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

set(SHUBA_GENERATED_POT
	"${CMAKE_BINARY_DIR}/shuba-ui.generated.pot")

add_custom_target(shuba_validate_localization
	COMMAND "$<TARGET_FILE:shuba_localization_contract>" --write-pot
		"${SHUBA_GENERATED_POT}"
	COMMAND "${CMAKE_COMMAND}" -E compare_files
		"${SHUBA_GENERATED_POT}"
		"${CMAKE_CURRENT_SOURCE_DIR}/Localization/shuba-ui.pot"
	COMMAND "${SHUBA_MSGFMT_EXECUTABLE}"
		--check --check-format --check-header --output-file=/dev/null
		"${CMAKE_CURRENT_SOURCE_DIR}/Localization/ru.po"
	COMMAND "${SHUBA_MSGCMP_EXECUTABLE}" --no-fuzzy-matching
		"${CMAKE_CURRENT_SOURCE_DIR}/Localization/ru.po"
		"${CMAKE_CURRENT_SOURCE_DIR}/Localization/shuba-ui.pot"
	COMMAND "$<TARGET_FILE:shuba_localization_contract>" --validate-catalog
		"${CMAKE_CURRENT_SOURCE_DIR}/Localization/ru.po"
	COMMAND "$<TARGET_FILE:shuba_localization_contract>" --run-failure-probes
		"${CMAKE_CURRENT_SOURCE_DIR}/Localization/ru.po"
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	DEPENDS shuba_localization_contract
		"${CMAKE_CURRENT_SOURCE_DIR}/Localization/ru.po"
		"${CMAKE_CURRENT_SOURCE_DIR}/Localization/shuba-ui.pot"
	COMMENT "Validating the tracked Shuba localization catalog contract"
	VERBATIM)

if(SHUBA_CPPCHECK_EXECUTABLE)
	add_custom_target(shuba_cppcheck
		COMMAND "${CMAKE_COMMAND}" -E make_directory
			"${CMAKE_BINARY_DIR}/cppcheck"
		COMMAND "${SHUBA_CPPCHECK_EXECUTABLE}"
			"--project=${CMAKE_BINARY_DIR}/compile_commands.json"
			"--file-filter=${CMAKE_SOURCE_DIR}/Source/*"
			"--file-filter=${CMAKE_SOURCE_DIR}/tests/*"
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
			${SHUBA_CLANG_TIDY_EXTRA_ARGS}
			-checks=${SHUBA_CLANG_TIDY_CHECKS}
			-header-filter=${SHUBA_ANALYZER_SOURCE_FILTER}
			-warnings-as-errors=${SHUBA_CLANG_TIDY_WARNINGS_AS_ERRORS}
			${SHUBA_ANALYZER_FILE_REGEX}
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		COMMENT "Running clang-tidy over Shuba host C++ targets"
		DEPENDS shuba_localization_resources
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
