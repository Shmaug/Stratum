cmake_minimum_required (VERSION 3.0)

set(DCMTK_DIR $ENV{DCMTK_DIR})

find_package(DCMTK REQUIRED COMPONENTS ofstd oflog dcmdata dcmimgle)

add_library(DicomVis MODULE DicomVis.cpp ImageLoader.cpp RenderVolume.cpp)
target_compile_definitions(DicomVis PUBLIC ${STM_COMPILE_DEFINITIONS})
target_include_directories(DicomVis PUBLIC ${STRATUM_DIR}/src PRIVATE ${DCMTK_INCLUDE_DIRS})
target_link_libraries(DicomVis PUBLIC Stratum PRIVATE ${DCMTK_LIBRARIES})


list(APPEND DCMVIS_SHADERS_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/Shaders/precompute.hlsl
  ${CMAKE_CURRENT_LIST_DIR}/Shaders/volume.hlsl )
set(DCMVIS_SHADERS_BIN ${STRATUM_DIR}/Assets/dcmvis_shaders.spvm)
add_custom_command(
  OUTPUT ${DCMVIS_SHADERS_BIN}
  COMMAND ShaderCompiler -o ${DCMVIS_SHADERS_BIN} -I ${STRATUM_DIR}/src/Shaders/include ${DCMVIS_SHADERS_SOURCES}
  DEPENDS ${DCMVIS_SHADERS_SOURCES} IMPLICIT_DEPENDS c ${DCMVIS_SHADERS_SOURCES})
add_custom_target(DicomVisShaders ALL DEPENDS ${DCMVIS_SHADERS_SOURCES} ShaderCompiler ${DCMVIS_SHADERS_BIN})
add_dependencies(DicomVis DicomVisShaders)


install(TARGETS DicomVis LIBRARY DESTINATION bin/Plugins ARCHIVE DESTINATION lib/Plugins RUNTIME DESTINATION bin/Plugins)