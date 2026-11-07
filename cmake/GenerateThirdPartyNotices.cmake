# Script mode: concatenates vcpkg's per-port copyright files into one notices file.
#   cmake -DINSTALLED_DIR=<vcpkg_installed/triplet> -DOUTPUT=<file> [-DEXTRA_NOTICE=<file>] -P ...
cmake_minimum_required(VERSION 3.19)

if(NOT DEFINED INSTALLED_DIR OR NOT DEFINED OUTPUT)
	message(FATAL_ERROR "INSTALLED_DIR and OUTPUT are required")
endif()

file(GLOB copyright_files "${INSTALLED_DIR}/share/*/copyright")
list(SORT copyright_files CASE INSENSITIVE)

set(notices "NexaMap Editor third-party notices\n\n")
string(APPEND notices
	"This distribution contains the libraries listed below. Each section reproduces the\n"
	"copyright and license text its project ships. This file does not state the terms\n"
	"of NexaMap Editor itself; see LICENSE.rtf.\n")

set(port_count 0)
set(body "")
foreach(copyright_file IN LISTS copyright_files)
	get_filename_component(port_dir "${copyright_file}" DIRECTORY)
	get_filename_component(port "${port_dir}" NAME)
	# vcpkg's own CMake helper ports are build tooling, not distributed code.
	if(port MATCHES "^vcpkg-")
		continue()
	endif()

	set(version "unknown version")
	if(EXISTS "${port_dir}/vcpkg.spdx.json")
		file(READ "${port_dir}/vcpkg.spdx.json" spdx)
		string(JSON package_count ERROR_VARIABLE json_error LENGTH "${spdx}" packages)
		if(NOT json_error AND package_count GREATER 0)
			string(JSON version ERROR_VARIABLE json_error GET "${spdx}" packages 0 versionInfo)
			if(json_error)
				set(version "unknown version")
			endif()
		endif()
	endif()

	file(READ "${copyright_file}" text)
	string(APPEND body
		"\n================================================================================\n"
		"${port} ${version}\n"
		"================================================================================\n\n"
		"${text}\n")
	math(EXPR port_count "${port_count} + 1")
endforeach()

if(port_count EQUAL 0)
	message(FATAL_ERROR "No vcpkg copyright files found under ${INSTALLED_DIR}/share")
endif()

string(APPEND notices "${body}")
if(DEFINED EXTRA_NOTICE AND EXISTS "${EXTRA_NOTICE}")
	file(READ "${EXTRA_NOTICE}" text)
	get_filename_component(extra_name "${EXTRA_NOTICE}" NAME)
	string(APPEND notices
		"\n================================================================================\n"
		"${extra_name}\n"
		"================================================================================\n\n"
		"${text}\n")
endif()

file(WRITE "${OUTPUT}" "${notices}")
message(STATUS "Wrote ${OUTPUT} (${port_count} packages)")
