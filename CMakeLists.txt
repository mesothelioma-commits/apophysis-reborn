cmake_minimum_required(VERSION 3.20)
project(apophysis_reborn VERSION 0.11.0 LANGUAGES CXX)
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
find_package(ZLIB REQUIRED)
option(APO_ENABLE_VULKAN "Enable dynamically loaded Vulkan compute rendering" ON)
# Lua is compiled as C++ so protected calls unwind C++ host callbacks safely.
set(apophysis_lua_units lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem
    lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio
    lauxlib lbaselib lmathlib lstrlib ltablib lutf8lib)
set(apophysis_lua_sources)
foreach(unit IN LISTS apophysis_lua_units)
    list(APPEND apophysis_lua_sources "third_party/lua/src/${unit}.c")
endforeach()
set_source_files_properties(${apophysis_lua_sources} PROPERTIES LANGUAGE CXX)
add_library(apo-lua STATIC ${apophysis_lua_sources})
target_include_directories(apo-lua PUBLIC third_party/lua/src)
set_target_properties(apo-lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(flame-core src/flame.cpp src/flame_io.cpp src/render.cpp src/image.cpp
    src/legacy_document.cpp src/output_file.cpp src/creative.cpp src/gpu_kernel.cpp src/vulkan_runtime.cpp
    src/render_file.cpp src/image_stream.cpp src/plugin.cpp src/project_io.cpp
    src/project_document.cpp src/legacy_formats.cpp src/script.cpp src/legacy_script.cpp
    src/script_document.cpp src/script_host.cpp src/color_curves.cpp
    src/animation.cpp src/animation_io.cpp src/animation_export.cpp src/sha256.cpp src/build_info.cpp)
target_include_directories(flame-core PUBLIC src)
target_include_directories(flame-core PRIVATE third_party/vulkan/include)
target_include_directories(flame-core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(flame-core PRIVATE APO_ENABLE_VULKAN=$<BOOL:${APO_ENABLE_VULKAN}>)
target_compile_definitions(flame-core PRIVATE APO_BUILD_CONFIGURATION="$<CONFIG>")
target_link_libraries(flame-core PRIVATE ${apophysis_expat_target} Threads::Threads ${CMAKE_DL_LIBS} ZLIB::ZLIB apo-lua)
add_executable(flame-cpu src/main.cpp src/document_cli.cpp src/extension_cli.cpp src/animation_cli.cpp)
target_link_libraries(flame-cpu PRIVATE flame-core)
if(WIN32)
    target_link_libraries(flame-cpu PRIVATE shell32)
endif()
include(CTest)
option(APO_BUILD_EDITOR "Build the native Qt desktop editor" ON)
if(APO_BUILD_EDITOR)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
    add_library(flame-editor src/editor/document_session.cpp src/editor/canvases.cpp
        src/editor/preview_controller.cpp src/editor/main_window.cpp src/editor/creative_widgets.cpp
        src/editor/render_queue.cpp src/editor/script_dialog.cpp src/editor/plugin_dialog.cpp src/editor/main_window_extensions.cpp
        src/editor/color_curve_dialog.cpp src/editor/color_curve_dialog.hpp
        src/editor/animation_dialog.cpp src/editor/animation_dialog.hpp src/editor/main_window_animation.cpp
        src/editor/support_dialogs.cpp src/editor/support_resources.qrc)
    set_target_properties(flame-editor PROPERTIES AUTOMOC ON AUTORCC ON)
    target_include_directories(flame-editor PUBLIC src)
    target_link_libraries(flame-editor PUBLIC flame-core Qt6::Widgets)
    add_executable(apophysis-reborn WIN32 src/editor/main.cpp src/editor/extensions_smoke.cpp src/editor/animation_smoke.cpp src/editor/support_smoke.cpp src/editor/editor_resources.qrc)
    set_target_properties(apophysis-reborn PROPERTIES AUTORCC ON)
    target_link_libraries(apophysis-reborn PRIVATE flame-editor)
    if(BUILD_TESTING)
        find_package(Qt6 REQUIRED COMPONENTS Test)
        foreach(test_name IN ITEMS preview document_session canvas creative_editor render_queue extensions_editor color_editor animation_editor support_editor)
            string(REPLACE "_" "-" target_name "${test_name}-tests")
            add_executable(${target_name} tests/${test_name}_tests.cpp)
            set_target_properties(${target_name} PROPERTIES AUTOMOC ON)
            target_link_libraries(${target_name} PRIVATE flame-editor Qt6::Test)
            add_test(NAME ${test_name} COMMAND ${target_name})
            set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        endforeach()
        add_executable(plugin-dialog-tests tests/plugin_dialog_tests.cpp)
        set_target_properties(plugin-dialog-tests PROPERTIES AUTOMOC ON)
        target_link_libraries(plugin-dialog-tests PRIVATE flame-editor Qt6::Test)
        add_dependencies(plugin-dialog-tests reborn-wave)
        add_test(NAME plugin_dialog COMMAND plugin-dialog-tests
            "${CMAKE_CURRENT_BINARY_DIR}/plugins/reborn-wave/reborn_wave.apoplugin")
        set_tests_properties(plugin_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(editor-tests tests/editor_tests.cpp src/editor/editor_resources.qrc)
        set_target_properties(editor-tests PROPERTIES AUTOMOC ON AUTORCC ON)
        target_link_libraries(editor-tests PRIVATE flame-editor Qt6::Test)
        add_test(NAME editor_workflow COMMAND editor-tests)
        set_tests_properties(editor_workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_test(NAME editor_startup COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:apophysis-reborn> -DVERSION=${PROJECT_VERSION}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/editor-startup -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_startup.cmake)
    endif()
endif()
# A complete loadable SDK sample is shipped with each desktop checkpoint.
set_source_files_properties(examples/plugins/reborn-wave/reborn_wave.c PROPERTIES LANGUAGE CXX)
add_library(reborn-wave SHARED examples/plugins/reborn-wave/reborn_wave.c)
target_include_directories(reborn-wave PRIVATE src)
set_target_properties(reborn-wave PROPERTIES PREFIX "" OUTPUT_NAME "reborn_wave"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins/reborn-wave"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins/reborn-wave")
set(APO_PLUGIN_LIBRARY "reborn_wave${CMAKE_SHARED_LIBRARY_SUFFIX}")
configure_file(examples/plugins/reborn-wave/reborn_wave.apoplugin.in
    "${CMAKE_CURRENT_BINARY_DIR}/plugins/reborn-wave/reborn_wave.apoplugin" @ONLY)
if(BUILD_TESTING)
    # Python is a build/release verification dependency; the installed application
    # and its portable runtime do not depend on Python.
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    foreach(apo_release_suite IN ITEMS staging benchmark packaging source_package build_identity build_scripts)
        add_test(NAME release_${apo_release_suite}
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/${apo_release_suite}_tests.py")
        set_tests_properties(release_${apo_release_suite} PROPERTIES
            ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1;APO_TEST_CMAKE=${CMAKE_COMMAND}"
            LABELS "release" TIMEOUT 180)
    endforeach()
    math(EXPR apo_pointer_bits "${CMAKE_SIZEOF_VOID_P} * 8")
    add_test(NAME build_info_cli COMMAND ${CMAKE_COMMAND}
        -DEXE=$<TARGET_FILE:flame-cpu> -DVERSION=${PROJECT_VERSION} -DVULKAN=${APO_ENABLE_VULKAN}
        -DEDITOR=${APO_BUILD_EDITOR} -DPOINTER_BITS=${apo_pointer_bits}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_info_cli.cmake)
    add_executable(animation-tests tests/animation_tests.cpp)
    target_link_libraries(animation-tests PRIVATE flame-core)
    add_dependencies(animation-tests reborn-wave plugin-fixture)
    add_test(NAME animation COMMAND animation-tests
        "${CMAKE_CURRENT_BINARY_DIR}/plugins/reborn-wave/reborn_wave.apoplugin" $<TARGET_FILE:plugin-fixture>)
    add_executable(animation-export-tests tests/animation_export_tests.cpp)
    target_link_libraries(animation-export-tests PRIVATE flame-core)
    add_test(NAME animation_export COMMAND animation-export-tests)
    add_executable(color-curves-tests tests/color_curves_tests.cpp)
    target_link_libraries(color-curves-tests PRIVATE flame-core)
    add_test(NAME color_curves COMMAND color-curves-tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/color)
    add_test(NAME animation_cli COMMAND ${CMAKE_COMMAND}
        -DEXE=$<TARGET_FILE:flame-cpu> -DSAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/ember-spiral.flame
        -DMANIFEST=${CMAKE_CURRENT_BINARY_DIR}/plugins/reborn-wave/reborn_wave.apoplugin
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/animation-cli -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/animation_cli.cmake)
    foreach(test_name IN ITEMS project_io legacy_formats script script_host)
        string(REPLACE "_" "-" target_name "${test_name}-tests")
        add_executable(${target_name} tests/${test_name}_tests.cpp)
        target_link_libraries(${target_name} PRIVATE flame-core)
        if(test_name STREQUAL "legacy_formats")
            add_test(NAME ${test_name} COMMAND ${target_name} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/formats)
        else()
            add_test(NAME ${test_name} COMMAND ${target_name})
        endif()
    endforeach()
    add_library(plugin-fixture MODULE tests/plugins/fixture_plugin.cpp)
    add_library(plugin-bad-abi MODULE tests/plugins/fixture_plugin.cpp)
    target_include_directories(plugin-fixture PRIVATE src)
    target_include_directories(plugin-bad-abi PRIVATE src)
    target_compile_definitions(plugin-bad-abi PRIVATE APO_TEST_BAD_ABI=1)
    add_executable(plugin-tests tests/plugin_tests.cpp)
    target_link_libraries(plugin-tests PRIVATE flame-core)
    add_dependencies(plugin-tests reborn-wave plugin-fixture plugin-bad-abi)
    add_test(NAME plugins COMMAND plugin-tests
        "${CMAKE_CURRENT_BINARY_DIR}/plugins/reborn-wave/reborn_wave.apoplugin"
        $<TARGET_FILE:plugin-fixture> $<TARGET_FILE:plugin-bad-abi>)
    add_test(NAME extensions_cli COMMAND ${CMAKE_COMMAND}
        -DEXE=$<TARGET_FILE:flame-cpu> -DSAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/ember-spiral.flame
        -DSCRIPTS=${CMAKE_CURRENT_SOURCE_DIR}/examples/scripts
        -DMANIFEST=${CMAKE_CURRENT_BINARY_DIR}/plugins/reborn-wave/reborn_wave.apoplugin
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/extensions-cli -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/extensions_cli.cmake)
    add_executable(file-render-tests tests/file_render_tests.cpp)
    target_link_libraries(file-render-tests PRIVATE flame-core)
    add_test(NAME file_render COMMAND file-render-tests)
    add_test(NAME file_cli COMMAND ${CMAKE_COMMAND}
        -DEXE=$<TARGET_FILE:flame-cpu> -DSAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/ember-spiral.flame
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/file-cli -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/file_cli.cmake)
    add_executable(image-stream-tests tests/image_stream_tests.cpp)
    target_link_libraries(image-stream-tests PRIVATE flame-core ZLIB::ZLIB)
    add_test(NAME image_stream COMMAND image-stream-tests)
    add_executable(gpu-stream-tests tests/gpu_stream_tests.cpp)
    target_link_libraries(gpu-stream-tests PRIVATE flame-core)
    add_test(NAME gpu_stream COMMAND gpu-stream-tests)
    set_tests_properties(gpu_stream PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(gpu-tests tests/gpu_tests.cpp)
    target_link_libraries(gpu-tests PRIVATE flame-core)
    add_test(NAME gpu COMMAND gpu-tests)
    set_tests_properties(gpu PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME gpu_cli COMMAND ${CMAKE_COMMAND}
        -DEXE=$<TARGET_FILE:flame-cpu> -DSAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/ember-spiral.flame
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/gpu-cli -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gpu_cli.cmake)
    add_executable(gpu-kernel-tests tests/gpu_kernel_tests.cpp)
    target_link_libraries(gpu-kernel-tests PRIVATE flame-core)
    add_test(NAME gpu_kernel_cpu COMMAND gpu-kernel-tests --cpu-only)
    add_test(NAME gpu_kernel COMMAND gpu-kernel-tests)
    set_tests_properties(gpu_kernel PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(creative-tests tests/creative_tests.cpp)
    target_link_libraries(creative-tests PRIVATE flame-core)
    add_test(NAME creative COMMAND creative-tests)
    add_executable(extended-variation-tests tests/extended_variation_tests.cpp tests/producer_variation_tests.cpp)
    target_link_libraries(extended-variation-tests PRIVATE flame-core)
    add_test(NAME extended_variations COMMAND extended-variation-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/extended-variation-vectors.tsv
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/variation-studies.flame
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/starting-variation-vectors.tsv)
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
include(cmake/BuildIdentity.cmake)
if(WIN32)
    enable_language(RC)
    foreach(apo_resource_name IN ITEMS flame-cpu apophysis-reborn)
        if(TARGET ${apo_resource_name})
            if(apo_resource_name STREQUAL "flame-cpu")
                set(apo_resource_description "Apophysis Reborn command-line renderer")
            else()
                set(apo_resource_description "Apophysis Reborn fractal flame editor")
            endif()
            configure_file(src/app_version.rc.in "generated/${apo_resource_name}-version.rc" @ONLY)
            target_sources(${apo_resource_name} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/${apo_resource_name}-version.rc")
        endif()
    endforeach()
endif()
