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
find_package(liblzma CONFIG REQUIRED)
find_package(Protobuf REQUIRED)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# LibArchive support is disabled by default. To enable OTGZ support, define
# OTGZ_SUPPORT in source/definitions.h and add LibArchive back to the build.
# if(APPLE)
#     set(CMAKE_PREFIX_PATH /usr/local/opt/libarchive)
# endif()
# find_package(LibArchive REQUIRED)

find_package(wxWidgets COMPONENTS html aui gl adv core net base REQUIRED)

find_package(ZLIB REQUIRED)

include(${wxWidgets_USE_FILE})
add_subdirectory(source/protobuf)
include(source/CMakeLists.txt)
if(WIN32)
	list(APPEND rme_SRC "${CMAKE_CURRENT_SOURCE_DIR}/vcproj/Project/Editor.rc")
endif()
add_executable(${PROJECT_NAME} ${rme_H} ${rme_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "NexaMap Editor")
if(WIN32)
	set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(ENABLE_UNITY_BUILD)
	if(CMAKE_VERSION VERSION_LESS 3.16)
		message(WARNING "ENABLE_UNITY_BUILD requires CMake 3.16 or newer; continuing without unity build")
	else()
		set_source_files_properties(
			"${CMAKE_CURRENT_SOURCE_DIR}/source/gl_renderer.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/source/post_process_renderer.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/source/spawn_format_map.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/source/procedural_map_generator.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/source/procedural_map_generator_window.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/source/minimap_import_window.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/source/png_map_import.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/source/png_map_import_window.cpp"
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
	Threads::Threads
	${ZLIB_LIBRARIES}
	glad::glad
	fmt::fmt
	nlohmann_json::nlohmann_json
	liblzma::liblzma
	nexamap_protobuf
)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
install(DIRECTORY
	"${CMAKE_CURRENT_SOURCE_DIR}/brushes"
	"${CMAKE_CURRENT_SOURCE_DIR}/data"
	"${CMAKE_CURRENT_SOURCE_DIR}/extensions"
	"${CMAKE_CURRENT_SOURCE_DIR}/icons"
	DESTINATION .
)
install(FILES
	"${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.rtf"
	"${CMAKE_CURRENT_SOURCE_DIR}/README.md"
	DESTINATION .
)

if(BUILD_TESTING)
	add_executable(border_learning_tests
		tests/border_learning_tests.cpp
		source/border_learning.cpp
	)
	target_include_directories(border_learning_tests PRIVATE source)
	set_target_properties(border_learning_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/border_learning_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/border_learning_tests"
	)
	add_test(NAME border_learning_tests COMMAND border_learning_tests)

	add_executable(minimap_import_tests
		tests/minimap_import_tests.cpp
		source/minimap_import.cpp
	)
	target_include_directories(minimap_import_tests PRIVATE source)
	target_link_libraries(minimap_import_tests PRIVATE ZLIB::ZLIB)
	set_target_properties(minimap_import_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/minimap_import_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/minimap_import_tests"
	)
	add_test(NAME minimap_import_tests COMMAND minimap_import_tests)

	add_executable(png_map_import_tests
		tests/png_map_import_tests.cpp
		source/png_map_import.cpp
	)
	target_include_directories(png_map_import_tests PRIVATE source)
	set_target_properties(png_map_import_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/png_map_import_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/png_map_import_tests"
	)
	add_test(NAME png_map_import_tests COMMAND png_map_import_tests)

	add_executable(border_topology_tests
		tests/border_topology_tests.cpp
		source/brush_tables.cpp
	)
	target_include_directories(border_topology_tests PRIVATE source)
	target_link_libraries(border_topology_tests PRIVATE ${wxWidgets_LIBRARIES})
	if(MSVC)
		target_compile_options(border_topology_tests PRIVATE /utf-8)
	endif()
	set_target_properties(border_topology_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/border_topology_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/border_topology_tests"
	)
	add_test(NAME border_topology_tests COMMAND border_topology_tests)

	add_executable(sprite_preloader_tests
		tests/sprite_preloader_tests.cpp
		source/sprite_preloader.cpp
	)
	target_include_directories(sprite_preloader_tests PRIVATE source)
	target_link_libraries(sprite_preloader_tests PRIVATE Threads::Threads)
	set_target_properties(sprite_preloader_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sprite_preloader_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/sprite_preloader_tests"
	)
	add_test(NAME sprite_preloader_tests COMMAND sprite_preloader_tests)

	add_executable(client_assets_manifest_tests
		tests/client_assets_manifest_tests.cpp
		source/client_assets_manifest.cpp
	)
	target_include_directories(client_assets_manifest_tests PRIVATE source)
	target_link_libraries(client_assets_manifest_tests PRIVATE nlohmann_json::nlohmann_json)
	set_target_properties(client_assets_manifest_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
	)
	add_test(NAME client_assets_manifest_tests COMMAND client_assets_manifest_tests)

	add_executable(map_format_tests
		tests/map_format_tests.cpp
		source/map_format.cpp
	)
	target_include_directories(map_format_tests PRIVATE source)
	target_link_libraries(map_format_tests PRIVATE ${wxWidgets_LIBRARIES})
	if(MSVC)
		target_compile_options(map_format_tests PRIVATE /utf-8)
	endif()
	set_target_properties(map_format_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
	)
	add_test(NAME map_format_tests COMMAND map_format_tests)

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

	add_executable(replace_rule_tests
		tests/replace_rule_tests.cpp
		source/replace_tool/replace_rule.cpp
	)
	target_include_directories(replace_rule_tests PRIVATE source)
	set_target_properties(replace_rule_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/replace_rule_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/replace_rule_tests"
	)
	add_test(NAME replace_rule_tests COMMAND replace_rule_tests)

	add_executable(replace_rule_manager_tests
		tests/replace_rule_manager_tests.cpp
		source/replace_tool/replace_rule.cpp
		source/replace_tool/replace_rule_manager.cpp
	)
	target_include_directories(replace_rule_manager_tests PRIVATE source)
	target_link_libraries(replace_rule_manager_tests PRIVATE nlohmann_json::nlohmann_json)
	set_target_properties(replace_rule_manager_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/replace_rule_manager_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/replace_rule_manager_tests"
	)
	add_test(NAME replace_rule_manager_tests COMMAND replace_rule_manager_tests)

	add_executable(replace_library_catalog_tests
		tests/replace_library_catalog_tests.cpp
		source/replace_tool/replace_library_catalog.cpp
	)
	target_include_directories(replace_library_catalog_tests PRIVATE source)
	set_target_properties(replace_library_catalog_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/replace_library_catalog_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/replace_library_catalog_tests"
	)
	add_test(NAME replace_library_catalog_tests COMMAND replace_library_catalog_tests)

	add_executable(replace_rule_editor_tests
		tests/replace_rule_editor_tests.cpp
		source/replace_tool/replace_rule.cpp
		source/replace_tool/replace_rule_editor.cpp
	)
	target_include_directories(replace_rule_editor_tests PRIVATE source)
	set_target_properties(replace_rule_editor_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/replace_rule_editor_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/replace_rule_editor_tests"
	)
	add_test(NAME replace_rule_editor_tests COMMAND replace_rule_editor_tests)

	add_executable(visual_similarity_index_tests
		tests/visual_similarity_index_tests.cpp
		source/replace_tool/visual_similarity_index.cpp
	)
	target_include_directories(visual_similarity_index_tests PRIVATE source)
	target_link_libraries(visual_similarity_index_tests PRIVATE Threads::Threads)
	set_target_properties(visual_similarity_index_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/visual_similarity_index_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/visual_similarity_index_tests"
	)
	add_test(NAME visual_similarity_index_tests COMMAND visual_similarity_index_tests)

	add_executable(replace_execution_plan_tests
		tests/replace_execution_plan_tests.cpp
		source/replace_tool/replace_rule.cpp
		source/replace_tool/replace_execution_plan.cpp
	)
	target_include_directories(replace_execution_plan_tests PRIVATE source)
	set_target_properties(replace_execution_plan_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/replace_execution_plan_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/replace_execution_plan_tests"
	)
	add_test(NAME replace_execution_plan_tests COMMAND replace_execution_plan_tests)

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

	add_executable(zones_tests
		tests/zones_tests.cpp
		source/zones.cpp
	)
	target_include_directories(zones_tests PRIVATE source)
	target_link_libraries(zones_tests PRIVATE ${wxWidgets_LIBRARIES})
	if(MSVC)
		target_compile_options(zones_tests PRIVATE /utf-8)
	endif()
	set_target_properties(zones_tests PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/zones_tests"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/zones_tests"
	)
	add_test(NAME zones_tests COMMAND zones_tests)
endif()
