cmake_minimum_required (VERSION 3.0)

find_package(DCMTK REQUIRED)

add_library(DicomVis MODULE "DicomVis.cpp" "ImageLoader.cpp" "RenderVolume.cpp")
target_compile_definitions(DicomVis PUBLIC ${STM_COMPILE_DEFINITIONS})
target_include_directories(DicomVis PUBLIC ${STRATUM_DIR} DCMTK_INCLUDE_DIRS)
target_link_libraries(DicomVis Stratum DCMTK_LIBRARIES)

stm_compile_shaders(ADD_DEPENDS DicomVis SHADER_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/precompute.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/volume.hlsl )