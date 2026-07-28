add_library(nexamap_protobuf appearances.proto)
target_link_libraries(nexamap_protobuf PUBLIC protobuf::libprotobuf)
target_include_directories(nexamap_protobuf PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
protobuf_generate(TARGET nexamap_protobuf LANGUAGE cpp)

if(MSVC)
	target_compile_options(nexamap_protobuf PRIVATE /wd4244 /wd4251 /wd4267)
endif()
