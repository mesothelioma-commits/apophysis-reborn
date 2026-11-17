cmake_minimum_required(VERSION 3.20)
project(apophysis_reborn VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(expat CONFIG QUIET)
if(TARGET expat::expat)
    set(apophysis_expat_target expat::expat)
else()
    find_package(EXPAT REQUIRED)
    set(apophysis_expat_target EXPAT::EXPAT)
endif()
find_package(Threads REQUIRED)
add_library(flame-core src/flame.cpp src/flame_io.cpp src/render.cpp src/image.cpp
    src/legacy_document.cpp src/output_file.cpp)
target_include_directories(flame-core PUBLIC src)
target_link_libraries(flame-core PRIVATE ${apophysis_expat_target} Threads::Threads)
add_executable(flame-cpu src/main.cpp src/document_cli.cpp)
target_link_libraries(flame-cpu PRIVATE flame-core)
if(WIN32)
    target_link_libraries(flame-cpu PRIVATE shell32)
endif()
include(CTest)
if(BUILD_TESTING)
    add_executable(reference-tests tests/reference_tests.cpp)
    target_link_libraries(reference-tests PRIVATE flame-core)
    add_test(NAME flam3_reference COMMAND reference-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference-vectors.tsv
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference-geometry.flame
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference-image.flame
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference-image.ppm)
    add_executable(legacy-document-tests tests/legacy_document_tests.cpp)
    target_link_libraries(legacy-document-tests PRIVATE flame-core)
    add_test(NAME legacy_document COMMAND legacy-document-tests)
    add_test(NAME producer_pixels COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:flame-cpu>
        -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy -DWORK=${CMAKE_CURRENT_BINARY_DIR}/producer-pixels
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/producer_pixels.cmake)
    add_test(NAME legacy_cli COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:flame-cpu>
        -DHISTORICAL=${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy/fox-3d-hack.flame
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/legacy-cli -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy_cli.cmake)
    add_executable(import-tests tests/import_tests.cpp)
    target_link_libraries(import-tests PRIVATE flame-core)
    add_test(NAME xml_import COMMAND import-tests)
    add_executable(renderer-tests tests/renderer_tests.cpp)
    target_link_libraries(renderer-tests PRIVATE flame-core)
    add_test(NAME renderer COMMAND renderer-tests)
    add_test(NAME cli_roundtrip COMMAND ${CMAKE_COMMAND}
        -DEXE=$<TARGET_FILE:flame-cpu> -DSAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/ember-spiral.flame
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-check -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_roundtrip.cmake)
    add_test(NAME foundation_cli COMMAND flame-cpu 64 64 100000 ${CMAKE_CURRENT_BINARY_DIR}/smoke.ppm)
    add_test(NAME flame_file_cli COMMAND flame-cpu --flame ${CMAKE_CURRENT_SOURCE_DIR}/examples/ember-spiral.flame
        --samples 100000 --threads 3 --output ${CMAKE_CURRENT_BINARY_DIR}/ember.bmp)
endif()
