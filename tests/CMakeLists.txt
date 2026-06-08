find_package(Catch2 3 CONFIG REQUIRED)

add_executable(shuba_headless_tests
	B02HarnessSmokeTests.cpp
	B03CorePrimitivesTests.cpp
	B04DomainModelTests.cpp
	B05MetadataSchemaTests.cpp
	B06JsonlCatalogTests.cpp)

target_compile_features(shuba_headless_tests PRIVATE cxx_std_23)
set_target_properties(shuba_headless_tests PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(shuba_headless_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")

target_link_libraries(shuba_headless_tests PRIVATE shuba_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(shuba_headless_tests)
