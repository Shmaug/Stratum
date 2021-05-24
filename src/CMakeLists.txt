cmake_minimum_required(VERSION 3.0)

# Stratum

file(GLOB_RECURSE STRATUM_CPP "Common/*.cpp" "Core/*.cpp" "NodeGraph/*.cpp")
add_executable(Stratum main.cpp ${STRATUM_CPP} stratum.rc)
set_target_properties(Stratum PROPERTIES ENABLE_EXPORTS TRUE)
target_link_libraries(Stratum PUBLIC OpenXR::openxr_loader)
target_compile_definitions(Stratum PRIVATE STRATUM_CORE)
target_compile_definitions(Stratum PUBLIC _USE_MATH_DEFINES)
target_compile_definitions(Stratum PUBLIC STRATUM_VERSION_MAJOR=1 STRATUM_VERSION_MINOR=3 )

option(ENABLE_DEBUG_LAYERS "Enable debug layers" TRUE)
if (${ENABLE_DEBUG_LAYERS})
	target_compile_definitions(Stratum PUBLIC ENABLE_DEBUG_LAYERS)
endif()

if (UNIX)
  target_link_libraries(Stratum PUBLIC xcb xcb-keysyms)
  target_compile_definitions(Stratum PUBLIC VK_USE_PLATFORM_XCB_KHR XR_USE_GRAPHICS_API_VULKAN)
elseif(WIN32)
	add_compile_options(/bigobj)
	target_compile_definitions(Stratum PUBLIC VK_USE_PLATFORM_WIN32_KHR WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

install(TARGETS Stratum LIBRARY DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)

# Shaders

file(GLOB_RECURSE SHADER_SOURCES "Shaders/**.hlsl")

foreach(SHADER_SRC ${SHADER_SOURCES})
  get_filename_component(SRC_NAME ${SHADER_SRC} NAME_WLE)
  file(STRINGS ${SHADER_SRC} LINES)
  foreach(LINE IN LISTS LINES)
    string(FIND "${LINE}" "#pragma compile" matchres)
    if(${matchres} EQUAL -1)
      continue()
    endif()

    separate_arguments(ARGS UNIX_COMMAND ${LINE})
    list(GET ARGS 2 SHADER_STAGE)
    list(GET ARGS 3 ENTRY_POINT)

    set(SPV_ASM_PATH "${STRATUM_DIR}/Assets/SPIR_V/${SRC_NAME}_${ENTRY_POINT}.spvasm")
    set(SPV_JSON_PATH "${STRATUM_DIR}/Assets/SPIR_V/${SRC_NAME}_${ENTRY_POINT}.json")

    add_custom_command(
      OUTPUT ${SPV_ASM_PATH}
      COMMAND dxc -nologo -spirv -fspv-target-env="vulkan1.2" -fspv-reflect -Zpr -Zi ${SHADER_SRC} -T ${SHADER_STAGE} -E ${ENTRY_POINT} -Fo ${SPV_ASM_PATH} -I ${STRATUM_DIR}/src/Shaders/include
      DEPENDS ${SHADER_SRC} IMPLICIT_DEPENDS c ${SHADER_SRC})
    add_custom_command(
      OUTPUT ${SPV_JSON_PATH}
      COMMAND spirv-cross ${SPV_ASM_PATH} --output ${SPV_JSON_PATH} --reflect
      DEPENDS ${SPV_ASM_PATH})
      
    add_custom_target(${SRC_NAME}_${ENTRY_POINT} ALL DEPENDS ${SPV_ASM_PATH} ${SPV_JSON_PATH})
    add_dependencies(Stratum ${SRC_NAME}_${ENTRY_POINT})

  endforeach()
endforeach()