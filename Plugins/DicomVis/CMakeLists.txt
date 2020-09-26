cmake_minimum_required (VERSION 3.0)

set(DCMTK_DIR $ENV{DCMTK_DIR})

find_package(DCMTK REQUIRED COMPONENTS ofstd oflog dcmdata dcmimgle)

add_library(DicomVis MODULE DicomVis.cpp ImageLoader.cpp RenderVolume.cpp)
target_compile_definitions(DicomVis PUBLIC ${STM_COMPILE_DEFINITIONS})
target_include_directories(DicomVis PUBLIC ${STRATUM_DIR}/src PRIVATE ${DCMTK_INCLUDE_DIRS})
target_link_libraries(DicomVis PUBLIC Stratum PRIVATE ${DCMTK_LIBRARIES})

stm_compile_shaders(TARGET_NAME DcmShaders SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/Shaders/precompute.hlsl
  ${CMAKE_CURRENT_LIST_DIR}/Shaders/volume.hlsl )
add_dependencies(DicomVis DcmShaders)

install(TARGETS DicomVis LIBRARY DESTINATION bin/Plugins ARCHIVE DESTINATION lib/Plugins RUNTIME DESTINATION bin/Plugins)