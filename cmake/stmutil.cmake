set(CMAKE_CXX_STANDARD 17)

function(stm_compile_definitions TARGET_NAME)
	target_include_directories(${TARGET_NAME} PUBLIC "${STRATUM_HOME}")

	target_compile_definitions(${TARGET_NAME} PUBLIC STRATUM_VERSION_MAJOR=1 STRATUM_VERSION_MINOR=1 XR_USE_GRAPHICS_API_VULKAN)
	if (${ENABLE_DEBUG_LAYERS})
		target_compile_definitions(${TARGET_NAME} PUBLIC ENABLE_DEBUG_LAYERS)
	endif()
	
	if (WIN32)
		target_compile_definitions(${TARGET_NAME} PUBLIC VK_USE_PLATFORM_WIN32_KHR WINDOWS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
		if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
			target_compile_definitions(${TARGET_NAME} PUBLIC _CRTDBG_MAP_ALLOC)
		endif()
	endif(WIN32)
endfunction()

function(stm_shader_target TARGET_NAME FOLDER_PATH)
	# Compile shaders in Shaders/* using ShaderCompiler
	file(GLOB_RECURSE SHADER_SOURCES
		"${FOLDER_PATH}*.frag"
		"${FOLDER_PATH}*.vert"
		"${FOLDER_PATH}*.glsl"
		"${FOLDER_PATH}*.hlsl" )

	foreach(SHADER ${SHADER_SOURCES})
		get_filename_component(FILE_NAME ${SHADER} NAME_WE)
		set(SPIRV "${PROJECT_BINARY_DIR}/bin/Shaders/${FILE_NAME}.stmb")

		add_custom_command(
			OUTPUT ${SPIRV}
			COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/bin/Shaders/"
			COMMAND "${PROJECT_BINARY_DIR}/bin/ShaderCompiler" ${SHADER} ${SPIRV} "${STRATUM_HOME}/Shaders"
			DEPENDS ${SHADER})

		list(APPEND SPIRV_BINARY_FILES ${SPIRV})
	endforeach(SHADER)

	add_custom_target(${TARGET_NAME} DEPENDS ${SPIRV_BINARY_FILES})
	add_dependencies(${TARGET_NAME} ShaderCompiler)
endfunction()