cmake_minimum_required(VERSION 3.28)

project(PistaForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8 /permissive- /Zc:__cplusplus /EHsc)
endif()

set(PF_VERSION "${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# FastWebUI (static library, no examples)
# ---------------------------------------------------------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WEBUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WEBUI_USE_TLS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/FastWebUI EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Core (no UI; depends only on the header-only JSON library)
# ---------------------------------------------------------------------------
add_library(pf_core STATIC
    src/core/geometry/bezier.cpp
    src/core/geometry/line.cpp
    src/core/geometry/arc.cpp
    src/core/geometry/index.cpp
    src/core/analysis/track_analysis.cpp
    src/core/sim/path_table.cpp
    src/core/sim/sim_doc.cpp
    src/core/scene/module.cpp
    src/core/scene/element.cpp
    src/core/scene/element_ops.cpp
    src/core/scene/scene.cpp
    src/core/scene/history.cpp
    src/core/scene/snap.cpp
    src/core/scene/validation.cpp
    src/core/io/scene_json.cpp
    src/core/io/scene_svg.cpp
    src/core/io/scene_pdf.cpp
    src/core/io/scene_obj.cpp
    src/core/io/robot_path.cpp
)
target_include_directories(pf_core PUBLIC src vendor)
target_compile_options(pf_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# ---------------------------------------------------------------------------
# Bridge (JSON dispatcher + API methods)
# ---------------------------------------------------------------------------
add_library(pf_bridge STATIC
    src/app/bridge/api.cpp
    src/app/bridge/api_doc.cpp
    src/app/bridge/dispatcher.cpp
    src/app/bridge/preferences.cpp
)
target_include_directories(pf_bridge PUBLIC src)
target_include_directories(pf_bridge PUBLIC vendor)
target_link_libraries(pf_bridge PUBLIC pf_core)
target_compile_options(pf_bridge PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
target_compile_definitions(pf_bridge PUBLIC PF_VERSION="${PF_VERSION}")

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(pf_app
    src/app/main.cpp
    src/app/shell/webui_shell.cpp
    src/app/shell/file_dialog.cpp
)
set_target_properties(pf_app PROPERTIES OUTPUT_NAME "PistaForge")
target_link_libraries(pf_app PRIVATE pf_bridge webui comdlg32)
target_compile_options(pf_app PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
target_compile_definitions(pf_app PRIVATE
    PF_SOURCE_WEB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/web"
)

# Icone e metadados do executavel (resources/pistaforge.rc).
if(WIN32)
    target_sources(pf_app PRIVATE resources/pistaforge.rc)
    target_include_directories(pf_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    set_source_files_properties(resources/pistaforge.rc PROPERTIES
        COMPILE_DEFINITIONS
            "PF_FILE_VERSION=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0;PF_FILE_FLAGS=0;PF_VERSION_STRING=\"${PF_VERSION}\""
    )
endif()

add_custom_command(TARGET pf_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/web"
        "$<TARGET_FILE_DIR:pf_app>/web"
    COMMENT "Copying web assets next to the executable"
)

set_target_properties(pf_app PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:pf_app>"
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(pf_tests
    tests/test_main.cpp
    tests/test_vec2.cpp
    tests/test_bezier.cpp
    tests/test_line.cpp
    tests/test_arc.cpp
    tests/test_index.cpp
    tests/test_scene.cpp
    tests/test_history.cpp
    tests/test_snap.cpp
    tests/test_element_ops.cpp
    tests/test_track_analysis.cpp
    tests/test_path_table.cpp
    tests/test_sim_doc.cpp
    tests/test_scene_pdf.cpp
    tests/test_scene_obj.cpp
    tests/test_robot_path.cpp
    tests/test_scene_json.cpp
    tests/test_svg.cpp
    tests/test_dispatcher.cpp
    tests/test_api_doc.cpp
    tests/test_preferences.cpp
)
target_link_libraries(pf_tests PRIVATE pf_bridge)
target_include_directories(pf_tests PRIVATE vendor)
target_compile_options(pf_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd5285>)
target_compile_definitions(pf_tests PRIVATE PF_VERSION="${PF_VERSION}")

add_test(NAME pf_tests COMMAND pf_tests)

# ---------------------------------------------------------------------------
# Distribuicao portatil (ZIP com executavel, assets web e runtime MSVC)
# ---------------------------------------------------------------------------
install(TARGETS pf_app RUNTIME DESTINATION .)
install(DIRECTORY src/web/ DESTINATION web)
install(FILES LICENSE README.md DESTINATION .)

# Copia as DLLs do runtime MSVC exigidas pelo executavel.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
include(InstallRequiredSystemLibraries)

set(CPACK_PACKAGE_NAME "PistaForge")
set(CPACK_PACKAGE_VENDOR "PistaForge")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Editor de pistas de corrida em C++23 e FastWebUI")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Felipe-chr/pistaforge")
set(CPACK_PACKAGE_VERSION "${PF_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "PistaForge-${PF_VERSION}-win64")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
