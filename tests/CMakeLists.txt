cmake_minimum_required(VERSION 3.28)
project(NavKitTests)

find_package(Catch2 CONFIG REQUIRED)

add_executable(NavKitTests
        tests.cpp
)
set_property(TARGET NavKitTests PROPERTY CXX_STANDARD 20)

target_link_libraries(NavKitTests PRIVATE
        Catch2::Catch2
        Catch2::Catch2WithMain
        NavKitLib
)

#file(COPY "${NavKit_SOURCE_DIR}/src/resource/DroidSans.ttf" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(NAV_WEAKNESS_DLL "${NavKit_SOURCE_DIR}/lib/Debug/NavWeakness.dll")
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(NAV_WEAKNESS_DLL "${NavKit_SOURCE_DIR}/lib/Release/NavWeakness.dll")
endif ()
add_custom_command(
        TARGET NavKitTests
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NAV_WEAKNESS_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/"
        COMMENT "Copying ${NAV_WEAKNESS_DLL} to ${CMAKE_BINARY_DIR}"
)
file(COPY "${NavKit_SOURCE_DIR}/lib/Debug/ResourceLib_HM3.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/lib/Debug/nfd.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
enable_testing()
add_test(NAME NavKitTests COMMAND NavKitTests)
set(TEST_RESOURCE_NAVP ${CMAKE_CURRENT_SOURCE_DIR}/resources/rectangles.navp)
set(TEST_RESOURCE_OBJ ${CMAKE_CURRENT_SOURCE_DIR}/resources/rectangles.obj)
add_custom_command(
        TARGET NavKitTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TEST_RESOURCE_OBJ}"
        $<TARGET_FILE_DIR:NavKitTests>
        COMMENT "Copying test resource rectangles.obj to output directory"
        VERBATIM
)
add_custom_command(
        TARGET NavKitTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TEST_RESOURCE_NAVP}"
        $<TARGET_FILE_DIR:NavKitTests>
        COMMENT "Copying test resource rectangles.navp to output directory"
        VERBATIM
)
