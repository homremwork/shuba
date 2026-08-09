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
	C30LocalizationPresentationLayoutTests.cpp)

target_compile_features(shuba_headless_tests PRIVATE cxx_std_23)
shuba_apply_common_target_options(shuba_headless_tests)

target_include_directories(shuba_headless_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")

target_link_libraries(shuba_headless_tests PRIVATE
	shuba_juce_platform
	juce::juce_core
	Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(shuba_headless_tests)

if(SHUBA_ENABLE_REAL_JPEG_XL_CODEC_TESTS)
	add_executable(shuba_jpeg_xl_codec_tests
		R05JpegXlCodecTests.cpp)
	target_compile_features(shuba_jpeg_xl_codec_tests PRIVATE cxx_std_23)
	target_link_libraries(shuba_jpeg_xl_codec_tests PRIVATE
		shuba_jpeg_xl_codec Catch2::Catch2WithMain)
	shuba_apply_common_target_options(shuba_jpeg_xl_codec_tests)
	catch_discover_tests(shuba_jpeg_xl_codec_tests)
endif()
