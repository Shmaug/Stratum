function(CompileShader SRC_PATH DST_FOLDER)
  get_filename_component(SRC_NAME ${SRC_PATH} NAME_WLE)
  file(STRINGS ${SRC_PATH} LINES)
  foreach(LINE IN LISTS LINES)
    string(FIND "${LINE}" "#pragma compile" R)
    if(NOT ${R} EQUAL 0)
      continue()
    endif()

    set(DST_NAME "${SRC_NAME}")

    separate_arguments(GLSLC_ARGS UNIX_COMMAND ${LINE})
    list(REMOVE_AT GLSLC_ARGS 0 1) # skip '#pragma compile'
    
    list(FIND GLSLC_ARGS "-source-entrypoint" ENTRY_POINT)
    if (${ENTRY_POINT} EQUAL -1)
      list(FIND GLSLC_ARGS "-sep" ENTRY_POINT)
      if (${ENTRY_POINT} EQUAL -1)
        list(FIND GLSLC_ARGS "-e" ENTRY_POINT)
      endif()
    endif()
    if (NOT ${ENTRY_POINT} EQUAL -1)
      math(EXPR ENTRY_POINT "${ENTRY_POINT}+1")
      list(GET GLSLC_ARGS ${ENTRY_POINT} ENTRY_POINT)
      set(DST_NAME "${SRC_NAME}_${ENTRY_POINT}")
    endif()
      
    set(SPV_PATH "${DST_FOLDER}/${DST_NAME}.spv")
    set(SPV_JSON_PATH "${DST_FOLDER}/${DST_NAME}.json")

    add_custom_command(OUTPUT "${SPV_PATH}"
      COMMAND glslangValidator -V ${GLSLC_ARGS} -g -I"${STRATUM_PATH}/src/Shaders/include" -o "${SPV_PATH}" "${SRC_PATH}"
      DEPENDS "${SRC_PATH}" IMPLICIT_DEPENDS c "${SRC_PATH}")
    add_custom_command(OUTPUT "${SPV_JSON_PATH}"
      COMMAND spirv-cross ${SPV_PATH} --output ${SPV_JSON_PATH} --reflect
      DEPENDS "${SPV_PATH}")
      
    add_custom_target(${DST_NAME} ALL DEPENDS "${SPV_JSON_PATH}")
    add_dependencies(Stratum ${DST_NAME})
  endforeach()
endfunction()