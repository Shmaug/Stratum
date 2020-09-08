cmake_minimum_required (VERSION 3.0)

add_library(DicomVis MODULE "DicomVis.cpp" "ImageLoader.cpp" "RenderVolume.cpp")
stm_compile_definitions(DicomVis)

add_dependencies(DicomVis Stratum)
target_link_libraries(DicomVis "${PROJECT_BINARY_DIR}/lib/Stratum.lib")

set_target_properties(DicomVis PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Plugins"
	LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Plugins"
	ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib/Plugins"
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/Plugins"
	LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/Plugins"
	ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/lib/Plugins"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Plugins"
	LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Plugins"
	ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib/Plugins")

if(WIN32)
	if(DEFINED ENV{DCMTK_HOME})
		message(STATUS "Found DCMTK_HOME: $ENV{DCMTK_HOME}")
	else()
		message(FATAL_ERROR "Error: DCMTK_HOME not set!")
	endif()
	target_include_directories(DicomVis PUBLIC "$ENV{DCMTK_HOME}/include")
	target_link_directories(DicomVis PUBLIC "$ENV{DCMTK_HOME}/lib")
	target_link_libraries(DicomVis "Netapi32.lib" "Iphlpapi.lib" "ofstd.lib" "oflog.lib" "dcmdata.lib" "dcmimgle.lib")
else()
	target_link_libraries(DicomVis "libz.so" "libofstd.so" "liboflog.so" "libdcmdata.so" "libdcmimgle.so")
endif()

stm_shader_target(DicomShaders "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/")
add_dependencies(DicomVis DicomShaders)