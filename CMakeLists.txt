cmake_minimum_required(VERSION 3.20)

project(Stratum)

# Find dependencies

find_package(Vulkan REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(assimp CONFIG)
find_package(OpenXR CONFIG)
find_package(OpenVDB CONFIG QUIET)

file(MAKE_DIRECTORY extern)
set(SLANG_ROOT "${CMAKE_CURRENT_LIST_DIR}/extern/slang")
if (NOT EXISTS ${SLANG_ROOT})
	message(STATUS "Downloading slang")
	if (WIN32)
		file(DOWNLOAD https://github.com/shader-slang/slang/releases/download/v0.23.10/slang-0.23.10-win64.zip "${SLANG_ROOT}.zip")
		message(STATUS "Extracting slang")
		file(ARCHIVE_EXTRACT INPUT "${SLANG_ROOT}.zip" DESTINATION "${SLANG_ROOT}")
		file(REMOVE "${SLANG_ROOT}.zip")
		else()
		file(DOWNLOAD https://github.com/shader-slang/slang/releases/download/v0.23.10/slang-0.23.10-linux-x86_64.zip "${SLANG_ROOT}.zip")
		message(STATUS "Extracting slang")
		file(ARCHIVE_EXTRACT INPUT "${SLANG_ROOT}.zip" DESTINATION "${SLANG_ROOT}")
		file(REMOVE "${SLANG_ROOT}.zip")
	endif()
endif()

set(SLANGC "")
if (WIN32)
	set(SLANGC "${SLANG_ROOT}/bin/windows-x64/release/slangc.exe")
else()
	set(SLANGC "${SLANG_ROOT}/bin/linux-x64/release/slangc")
endif()

add_subdirectory(src)