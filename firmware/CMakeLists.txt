cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${CMAKE_CURRENT_LIST_DIR}/../extensions")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nexus_pet)

execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../tools/board_codegen.py" --check
    RESULT_VARIABLE board_check)
if(NOT board_check EQUAL 0)
    message(FATAL_ERROR "Board configuration is stale; run tools/board_codegen.py")
endif()
