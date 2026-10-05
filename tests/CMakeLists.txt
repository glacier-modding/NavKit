enable_testing()

add_executable(ReasoningGridTests
        reasoning_grid_tests.cpp
        ../src/model/ReasoningGrid.cpp
)
add_test(NAME ReasoningGridTests COMMAND ReasoningGridTests)

add_executable(NavKitTests
        tests.cpp
        domain_tests.cpp
)
target_link_libraries(NavKitTests PRIVATE
        NavKitLib
)

set(TEST_DLLS)
if (WIN32)
    set(TEST_DLLS
            ${THIRD_PARTY_DLL_PATHS}
            "$<TARGET_FILE:navkit-rpkg-lib>"
    )
endif ()
if (TEST_DLLS)
    add_custom_command(
            TARGET NavKitTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${TEST_DLLS}
            $<TARGET_FILE_DIR:NavKitTests>/
            COMMENT "Copying runtime dependencies for tests..."
            VERBATIM
    )
endif ()

add_test(NAME NavKitTests COMMAND NavKitTests)
set_tests_properties(NavKitTests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
