enable_testing()

add_executable(ReasoningGridTests
        reasoning_grid_tests.cpp
)
target_link_libraries(ReasoningGridTests PRIVATE
        NavKitLib
)
target_compile_definitions(ReasoningGridTests PRIVATE
        NAVKIT_REASONING_GRID_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)
add_test(NAME ReasoningGridTests
        COMMAND ReasoningGridTests "${CMAKE_CURRENT_SOURCE_DIR}/resources/intro.airg"
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/intelcpudemo_official.airg"
)
set_tests_properties(ReasoningGridTests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

add_executable(NavKitTests
        tests.cpp
        domain_tests.cpp
)
target_link_libraries(NavKitTests PRIVATE
        NavKitLib
)

add_executable(GfxBuilderTests
        gfx_map_layer_tests.cpp
        ../src/util/GfxMapAlignment.cpp
        ../src/util/GfxMapLayerSwf.cpp
)
target_include_directories(GfxBuilderTests PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(GfxBuilderTests PRIVATE pugixml::pugixml glm::glm)
if (WIN32)
    set(PUGIXML_RUNTIME_PATH "${PROJECT_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
    add_custom_command(
            TARGET GfxBuilderTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${PUGIXML_RUNTIME_PATH}/debug/bin/pugixml.dll,${PUGIXML_RUNTIME_PATH}/bin/pugixml.dll>"
            "$<TARGET_FILE_DIR:GfxBuilderTests>"
            COMMENT "Copying runtime dependencies for GfxBuilderTests..."
            VERBATIM
    )
endif ()

option(NAVKIT_BUILD_INTEGRATION_TESTS "Build the manually run live game integration test" OFF)
option(NAVKIT_BUILD_DEMO_MOD_GENERATOR "Build the NavKit Demo Mod generator and its generate-demo-mod target" OFF)
if (NAVKIT_BUILD_INTEGRATION_TESTS)
    add_executable(NavKitIntegrationTests
            pipeline_tests.cpp
    )
    target_link_libraries(NavKitIntegrationTests PRIVATE
            NavKitBulk
    )
    target_compile_definitions(NavKitIntegrationTests PRIVATE
            NAVKIT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )
endif ()
if (NAVKIT_BUILD_DEMO_MOD_GENERATOR)
    set(NAVKIT_DEMO_MOD_DIR "" CACHE PATH
            "NavKitDemoMod folder to regenerate (default: <Hitman folder>/Simple Mod Framework/Mods/NavKitDemoMod)")
    set(NAVKIT_DEMO_MOD_ARGS "" CACHE STRING
            "Extra NavKitDemoModGenerator arguments for generate-demo-mod, such as --mission arrival;--smoketest;false")
    add_executable(NavKitDemoModGenerator
            demo_mod_generator.cpp
    )
    target_link_libraries(NavKitDemoModGenerator PRIVATE
            NavKitBulk
    )
    target_compile_definitions(NavKitDemoModGenerator PRIVATE
            NAVKIT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
            NAVKIT_DEMO_MOD_DIR="${NAVKIT_DEMO_MOD_DIR}"
            NAVKIT_DEMO_MOD_WORK_DIR="${CMAKE_CURRENT_BINARY_DIR}/demo-mod-work"
    )
    # Builds the generator, then regenerates the demo mod: launches HITMAN for each mission, builds the NAVP, AIRG and
    # CRMDs, writes them into the mod, deploys it with SMF, verifies the deployed RPKGs and loads each mission.
    add_custom_target(generate-demo-mod
            COMMAND NavKitDemoModGenerator ${NAVKIT_DEMO_MOD_ARGS}
            DEPENDS NavKitDemoModGenerator
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Regenerating the NavKit Demo Mod"
            USES_TERMINAL
            VERBATIM
    )
endif ()

set(TEST_DLLS)
set(TEST_TARGETS NavKitTests ReasoningGridTests)
if (WIN32)
    set(TEST_DLLS
            ${THIRD_PARTY_DLL_PATHS}
            "$<TARGET_FILE:navkit-rpkg-lib>"
    )
endif ()
if (NAVKIT_BUILD_INTEGRATION_TESTS)
    list(APPEND TEST_TARGETS NavKitIntegrationTests)
endif ()
if (NAVKIT_BUILD_DEMO_MOD_GENERATOR)
    list(APPEND TEST_TARGETS NavKitDemoModGenerator)
endif ()
if (TEST_DLLS)
    foreach (TEST_TARGET ${TEST_TARGETS})
        add_custom_command(
                TARGET ${TEST_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${TEST_DLLS}
                $<TARGET_FILE_DIR:${TEST_TARGET}>
                COMMENT "Copying runtime dependencies for ${TEST_TARGET}..."
                VERBATIM
        )
    endforeach ()
endif ()

add_test(NAME NavKitTests COMMAND NavKitTests)
set_tests_properties(NavKitTests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

add_test(NAME GfxBuilderTests COMMAND GfxBuilderTests)
set_tests_properties(GfxBuilderTests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

add_executable(UpdaterTests updater_tests.cpp)
target_include_directories(UpdaterTests PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME UpdaterTests COMMAND UpdaterTests)
