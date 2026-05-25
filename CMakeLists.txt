cmake_minimum_required(VERSION 3.1)

project(rme)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(asio CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

find_package(OpenGL REQUIRED)

# LibArchive support is disabled by default. To enable OTGZ support, define
# OTGZ_SUPPORT in source/definitions.h and add LibArchive back to the build.
# if(APPLE)
#     set(CMAKE_PREFIX_PATH /usr/local/opt/libarchive)
# endif()
# find_package(LibArchive REQUIRED)

find_package(wxWidgets COMPONENTS html aui gl adv core net base REQUIRED)

find_package(GLUT REQUIRED)
find_package(ZLIB REQUIRED)

include(${wxWidgets_USE_FILE})
include(source/CMakeLists.txt)
add_executable(${PROJECT_NAME} ${rme_H} ${rme_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)

include_directories(${OPENGL_INCLUDE_DIR} ${GLUT_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME}
	${wxWidgets_LIBRARIES}
	${OPENGL_LIBRARIES}
	${GLUT_LIBRARIES}
	${ZLIB_LIBRARIES}
	fmt::fmt
	asio::asio
	nlohmann_json::nlohmann_json
)
