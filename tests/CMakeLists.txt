find_package(Catch2 3 CONFIG REQUIRED)

add_executable(shuba_headless_tests
	B02HarnessSmokeTests.cpp
	B03CorePrimitivesTests.cpp
	B04DomainModelTests.cpp
	B05MetadataSchemaTests.cpp
	B06JsonlCatalogTests.cpp
	B07CatalogStorageTests.cpp
	B08CatalogRepositoryTests.cpp
	B09CatalogSearchTests.cpp
	B10PlatformServicesTests.cpp
	B11AndroidPlatformContractTests.cpp
	B12MediaCodecContractTests.cpp
	B13PhotoImportTests.cpp
	B14PhotoExportTests.cpp
	B15BackupArchiveTests.cpp
	B16CatalogReplacementTests.cpp
	B17CatalogSessionTests.cpp
	B18EntityEditTests.cpp
	B19PhotoWorkflowTests.cpp
	B20BackupRecoverySessionTests.cpp
	B21PendingPhotoStagingTests.cpp
	B22ImagePreviewCacheTests.cpp
	B23UiSafeAreaTests.cpp
	B24FatalStartupRecoveryTests.cpp
	B25AndroidPreviousExitTests.cpp
	B27LocalizationLanguageTests.cpp
	B28LocalizationFacadeTests.cpp
	B29PhotoCoordinatorLifetimeTests.cpp
	B30AppShellBackNavigationTests.cpp
	ShellOperationRunnerTests.cpp
	PostR14UiFeedbackCorrectionTests.cpp
	C30LocalizationPresentationLayoutTests.cpp)

target_compile_features(shuba_headless_tests PRIVATE cxx_std_23)
target_compile_definitions(shuba_headless_tests PRIVATE
	JUCE_MODAL_LOOPS_PERMITTED=1)
shuba_apply_common_target_options(shuba_headless_tests)

target_include_directories(shuba_headless_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")

target_link_libraries(shuba_headless_tests PRIVATE
	shuba_juce_platform
	juce::juce_core
	Catch2::Catch2WithMain)

include(Catch)
if(SHUBA_ENABLE_ASAN)
	set(SHUBA_LSAN_JUCE_FONTCONFIG_SUPPRESSION
		"${CMAKE_CURRENT_SOURCE_DIR}/../tools/ci/lsan-juce-fontconfig.supp")
	catch_discover_tests(shuba_headless_tests
		PROPERTIES ENVIRONMENT
			"LSAN_OPTIONS=suppressions=${SHUBA_LSAN_JUCE_FONTCONFIG_SUPPRESSION}:print_suppressions=1")
else()
	catch_discover_tests(shuba_headless_tests)
endif()

if(SHUBA_ENABLE_ASAN)
	add_executable(shuba_asan_lsan_policy_probe
		AsanLeakSanitizerPolicyProbe.cpp)
	target_compile_features(shuba_asan_lsan_policy_probe PRIVATE cxx_std_23)
	shuba_apply_common_target_options(shuba_asan_lsan_policy_probe)
	add_test(NAME "ASAN policy rejects unsuppressed project leaks"
		COMMAND ${CMAKE_COMMAND}
			"-DSHUBA_ASAN_LSAN_POLICY_PROBE=$<TARGET_FILE:shuba_asan_lsan_policy_probe>"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/RunAsanLeakSanitizerPolicyProbe.cmake")
	set_tests_properties("ASAN policy rejects unsuppressed project leaks"
		PROPERTIES
			ENVIRONMENT
				"LSAN_OPTIONS=suppressions=${SHUBA_LSAN_JUCE_FONTCONFIG_SUPPRESSION}:print_suppressions=1")
endif()

if(SHUBA_ENABLE_REAL_JPEG_XL_CODEC_TESTS)
	add_executable(shuba_jpeg_xl_codec_tests
		R05JpegXlCodecTests.cpp)
	target_compile_features(shuba_jpeg_xl_codec_tests PRIVATE cxx_std_23)
	target_link_libraries(shuba_jpeg_xl_codec_tests PRIVATE
		shuba_jpeg_xl_codec Catch2::Catch2WithMain)
	shuba_apply_common_target_options(shuba_jpeg_xl_codec_tests)
	catch_discover_tests(shuba_jpeg_xl_codec_tests)
endif()
