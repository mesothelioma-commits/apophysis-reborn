file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(environment "QT_QPA_PLATFORM=offscreen" "XDG_CONFIG_HOME=${WORK}/config"
    "XDG_DATA_HOME=${WORK}/data" "APPDATA=${WORK}/appdata" "LOCALAPPDATA=${WORK}/localappdata")
file(MAKE_DIRECTORY "${WORK}/occupied")
file(WRITE "${WORK}/occupied/keep.txt" "Existing user content.\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${environment} "${EXE}" --build-info --json
    RESULT_VARIABLE result OUTPUT_VARIABLE info ERROR_VARIABLE error TIMEOUT 15)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Editor build information failed: ${result} ${error}")
endif()
string(JSON version GET "${info}" version)
string(JSON editor GET "${info}" editor_compiled)
string(JSON qt GET "${info}" qt_version)
if(NOT version STREQUAL VERSION OR NOT editor OR qt STREQUAL "")
    message(FATAL_ERROR "Editor build information omitted its identity: ${info}")
endif()
foreach(arguments IN ITEMS "--animation-smoke-test;${WORK}/invalid;--backend;bogus"
                           "--animation-smoke-test;${WORK}/invalid;--smoke-test;${WORK}/other"
                           "--animation-smoke-test;${WORK}/invalid;user.flame"
                           "--support-smoke-test;${WORK}/invalid;--backend;bogus"
                           "--support-smoke-test;${WORK}/occupied"
                           "--build-info;--json;user.flame" "--json")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${environment} "${EXE}" ${arguments}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
    if(NOT result MATCHES "^[0-9]+$" OR result EQUAL 0 OR error STREQUAL "")
        message(FATAL_ERROR "Invalid automation arguments did not fail without a modal hang: ${result} ${output} ${error}")
    endif()
endforeach()
file(READ "${WORK}/occupied/keep.txt" retained)
if(NOT retained STREQUAL "Existing user content.\n")
    message(FATAL_ERROR "Verification startup changed existing user files")
endif()
file(GLOB_RECURSE outputs "${WORK}/*.png" "${WORK}/*.flame" "${WORK}/*.apoproject" "${WORK}/config/*")
if(outputs)
    message(FATAL_ERROR "Informational or rejected startup unexpectedly changed user data: ${outputs}")
endif()
