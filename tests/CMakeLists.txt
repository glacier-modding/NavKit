find_package(Catch2 CONFIG REQUIRED)

add_executable(ReasoningGridTests
        reasoning_grid_tests.cpp
        ../src/model/ReasoningGrid.cpp
)
add_test(NAME ReasoningGridTests COMMAND ReasoningGridTests)

add_executable(NavKitTests
        tests.cpp
)
target_link_libraries(NavKitTests PRIVATE
        Catch2::Catch2WithMain
        NavKitLib
)

set(TEST_DLLS)
if (WIN32)
    set(TEST_DLLS
            "${NavKit_SOURCE_DIR}/lib/$<CONFIG>/navkit_rpkg_lib.dll"
            "${NavKit_SOURCE_DIR}/lib/$<CONFIG>/nfd.dll"
    )
endif ()
set(TEST_RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/rectangles.navp"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/rectangles.obj"
)
add_custom_command(
        TARGET NavKitTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TEST_DLLS}
        ${TEST_RESOURCES}
        $<TARGET_FILE_DIR:NavKitTests>/
        COMMENT "Copying runtime dependencies and resources for tests..."
        VERBATIM
)

install(TARGETS NavKitTests
        RUNTIME DESTINATION bin
        COMPONENT Testing
)
install(FILES ${TEST_RESOURCES}
        DESTINATION bin/resources
        COMPONENT Testing
)

enable_testing()
add_test(NAME NavKitTests COMMAND NavKitTests)