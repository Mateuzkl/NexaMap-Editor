cmake_minimum_required(VERSION 3.10)

project(rme)

include(CTest)

option(ENABLE_UNITY_BUILD "Enable unity build for faster compilation" ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

find_package(OpenGL REQUIRED)

# LibArchive support is disabled by default. To enable OTGZ support, define
# OTGZ_SUPPORT in source/definitions.h and add LibArchive back to the build.
# if(APPLE)
#     set(CMAKE_PREFIX_PATH /usr/local/opt/libarchive)
# endif()
# find_package(LibArchive REQUIRED)

find_package(wxWidgets COMPONENTS html aui gl adv core net base REQUIRED)

find_package(ZLIB REQUIRED)

include(${wxWidgets_USE_FILE})
include(source/CMakeLists.txt)
add_executable(${PROJECT_NAME} ${rme_H} ${rme_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)
if(ENABLE_UNITY_BUILD)
	if(CMAKE_VERSION VERSION_LESS 3.16)
		message(WARNING "ENABLE_UNITY_BUILD requires CMake 3.16 or newer; continuing without unity build")
	else()
		set_source_files_properties(
			"${CMAKE_CURRENT_SOURCE_DIR}/source/spawn_format_map.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/source/procedural_map_generator.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/source/procedural_map_generator_window.cpp"
			PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
		)
		set_target_properties(${PROJECT_NAME} PROPERTIES
			UNITY_BUILD ON
			UNITY_BUILD_BATCH_SIZE 8
		)
	endif()
endif()

include_directories(${OPENGL_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME}
	${wxWidgets_LIBRARIES}
	${OPENGL_LIBRARIES}
	${ZLIB_LIBRARIES}
	glad::glad
	fmt::fmt
	nlohmann_json::nlohmann_json
)

if(BUILD_TESTING)
	add_executable(item_id_mapping_tests
		tests/item_id_mapping_tests.cpp
		source/item_id_mapping.cpp
	)
	target_include_directories(item_id_mapping_tests PRIVATE source)
	set_target_properties(item_id_mapping_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/item_id_mapping_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/item_id_mapping_tests"
	)
	add_test(NAME item_id_mapping_tests COMMAND item_id_mapping_tests)

	add_executable(depot_conversion_validation_tests
		tests/depot_conversion_validation_tests.cpp
		source/depot_conversion_validation.cpp
	)
	target_include_directories(depot_conversion_validation_tests PRIVATE source)
	set_target_properties(depot_conversion_validation_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/depot_conversion_validation_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/depot_conversion_validation_tests"
	)
	add_test(NAME depot_conversion_validation_tests COMMAND depot_conversion_validation_tests)

	add_executable(spawn_format_tests
		tests/spawn_format_tests.cpp
		source/spawn_format.cpp
	)
	target_include_directories(spawn_format_tests PRIVATE source)
	set_target_properties(spawn_format_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/spawn_format_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/spawn_format_tests"
	)
	add_test(NAME spawn_format_tests COMMAND spawn_format_tests)

	add_executable(spawn_xml_converter_tests
		tests/spawn_xml_converter_tests.cpp
		source/spawn_xml_converter.cpp
	)
	target_include_directories(spawn_xml_converter_tests PRIVATE source)
	set_target_properties(spawn_xml_converter_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/spawn_xml_converter_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/spawn_xml_converter_tests"
	)
	add_test(NAME spawn_xml_converter_tests COMMAND spawn_xml_converter_tests)

	add_executable(procedural_map_generator_tests
		tests/procedural_map_generator_tests.cpp
		source/procedural_map_generator.cpp
	)
	target_include_directories(procedural_map_generator_tests PRIVATE source)
	set_target_properties(procedural_map_generator_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/procedural_map_generator_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/procedural_map_generator_tests"
	)
	add_test(NAME procedural_map_generator_tests COMMAND procedural_map_generator_tests)
endif()
