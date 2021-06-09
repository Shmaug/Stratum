cmake_minimum_required(VERSION 3.0)

# Stratum

file(GLOB_RECURSE STRATUM_CPP "Common/*.cpp" "Core/*.cpp" "NodeGraph/*.cpp")
add_executable(Stratum main.cpp ${STRATUM_CPP} stratum.rc)
set_target_properties(Stratum PROPERTIES ENABLE_EXPORTS TRUE)
target_link_libraries(Stratum PUBLIC OpenXR::openxr_loader)
target_compile_definitions(Stratum PRIVATE STRATUM_CORE )
target_compile_definitions(Stratum PUBLIC _USE_MATH_DEFINES STRATUM_VERSION_MAJOR=1 STRATUM_VERSION_MINOR=3 )

if (UNIX)
  target_link_libraries(Stratum PUBLIC xcb xcb-keysyms)
  target_compile_definitions(Stratum PUBLIC VK_USE_PLATFORM_XCB_KHR XR_USE_GRAPHICS_API_VULKAN)
elseif(WIN32)
	add_compile_options(/bigobj)
	target_compile_definitions(Stratum PUBLIC VK_USE_PLATFORM_WIN32_KHR WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

option(STRATUM_ENABLE_DEBUG_LAYERS "Enable debug layers" TRUE)
if (${STRATUM_ENABLE_DEBUG_LAYERS})
	target_compile_definitions(Stratum PUBLIC STRATUM_ENABLE_DEBUG_LAYERS)
endif()

install(TARGETS Stratum LIBRARY DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)

file(GLOB_RECURSE SHADER_FILES "Shaders/**.[gh]lsl")
foreach(SHADER_FILE ${SHADER_FILES})
  CompileShader(${SHADER_FILE} ${STRATUM_PATH}/Assets/SPIR-V)
endforeach()