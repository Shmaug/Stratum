cmake_minimum_required (VERSION 3.0)

set(DCMTK_DIR $ENV{DCMTK_DIR})

find_package(DCMTK REQUIRED COMPONENTS ofstd oflog dcmdata dcmimgle)

add_library(DicomVis SHARED DicomVis.cpp ImageLoader.cpp VolumeRenderer.cpp)
target_compile_definitions(DicomVis PUBLIC ${STM_COMPILE_DEFINITIONS})
target_include_directories(DicomVis PUBLIC ${STRATUM_PATH}/src PRIVATE ${DCMTK_INCLUDE_DIRS})
target_link_libraries(DicomVis PUBLIC Stratum PRIVATE ${DCMTK_LIBRARIES})


file(GLOB_RECURSE DCMVIS_SHADERS_SOURCES "./**.[gh]lsl")
foreach(SHADER_FILE ${DCMVIS_SHADERS_SOURCES})
  CompileShader(${SHADER_FILE} ${STRATUM_PATH}/Assets/SPIR-V)
endforeach()

install(TARGETS DicomVis LIBRARY DESTINATION bin/Plugins ARCHIVE DESTINATION lib/Plugins RUNTIME DESTINATION bin/Plugins)