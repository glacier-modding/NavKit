enable_testing()

add_executable(ReasoningGridTests
        reasoning_grid_tests.cpp
        ../src/model/ReasoningGrid.cpp
)
target_compile_definitions(ReasoningGridTests PRIVATE
        NAVKIT_REASONING_GRID_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)
add_test(NAME ReasoningGridTests
        COMMAND ReasoningGridTests "${CMAKE_CURRENT_SOURCE_DIR}/resources/intro.airg"
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/intelcpudemo.airg"
)
set_tests_properties(ReasoningGridTests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

add_executable(NavKitTests
        tests.cpp
        domain_tests.cpp
)
target_link_libraries(NavKitTests PRIVATE
        NavKitLib
)

option(NAVKIT_BUILD_INTEGRATION_TESTS "Build the manually run live game integration test" OFF)
if (NAVKIT_BUILD_INTEGRATION_TESTS)
    add_executable(NavKitIntegrationTests
            pipeline_tests.cpp
    )
    target_link_libraries(NavKitIntegrationTests PRIVATE
            NavKitLib
    )
    target_compile_definitions(NavKitIntegrationTests PRIVATE
            NAVKIT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )
endif ()

set(TEST_DLLS)
set(TEST_TARGETS NavKitTests)
if (WIN32)
    set(TEST_DLLS
            ${THIRD_PARTY_DLL_PATHS}
            "$<TARGET_FILE:navkit-rpkg-lib>"
    )
endif ()
if (NAVKIT_BUILD_INTEGRATION_TESTS)
    list(APPEND TEST_TARGETS NavKitIntegrationTests)
endif ()
if (TEST_DLLS)
    foreach (TEST_TARGET ${TEST_TARGETS})
        add_custom_command(
                TARGET ${TEST_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${TEST_DLLS}
                $<TARGET_FILE_DIR:${TEST_TARGET}>/
                COMMENT "Copying runtime dependencies for ${TEST_TARGET}..."
                VERBATIM
        )
    endforeach ()
endif ()

add_test(NAME NavKitTests COMMAND NavKitTests)
set_tests_properties(NavKitTests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
