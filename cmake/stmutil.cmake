# Populate STM_COMPILE_DEFINITIONS
list(APPEND STM_COMPILE_DEFINITIONS STRATUM_VERSION_MAJOR=${STRATUM_VERSION_MAJOR} STRATUM_VERSION_MINOR=${STRATUM_VERSION_MINOR} XR_USE_GRAPHICS_API_VULKAN)
if (${ENABLE_DEBUG_LAYERS})
	list(APPEND STM_COMPILE_DEFINITIONS ENABLE_DEBUG_LAYERS)
endif()
if (WIN32)
	list(APPEND STM_COMPILE_DEFINITIONS WINDOWS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS VK_USE_PLATFORM_WIN32_KHR)
endif(WIN32)

function(stm_compile_shaders)
	cmake_parse_arguments(ARG "" "TARGET_NAME" "SOURCES" ${ARGN})

	set(DST_FOLDER ${STRATUM_DIR}/Assets/Shaders)
	set(STM_INCLUDE_DIR ${STRATUM_DIR}/src/Shaders/include)

	foreach(SRC ${ARG_SOURCES})
		get_filename_component(SHADER_NAME ${SRC} NAME_WE)
		set(DST ${DST_FOLDER}/${SHADER_NAME}.stmb)

		add_custom_command(
			OUTPUT ${DST}
			DEPENDS ${SRC}
			IMPLICIT_DEPENDS c ${SRC}
			COMMAND ShaderCompiler ${SRC} ${DST} ${STM_INCLUDE_DIR})

		list(APPEND SHADER_BINARIES ${DST})
	endforeach()
			
	add_custom_target(${ARG_TARGET_NAME} DEPENDS ${SHADER_BINARIES})
endfunction()