﻿cmake_minimum_required(VERSION 3.28.6)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>" CACHE PATH "Runtime output directory" FORCE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>" CACHE PATH "Library output directory" FORCE)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/lib" CACHE PATH "Archive output directory" FORCE)

# Enable Hot Reload for MSVC compilers if supported.
if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS_DEBUG "/ZI")
    set(CMAKE_SHARED_LINKER_FLAGS "/SAFESEH:NO")
    set(CMAKE_EXE_LINKER_FLAGS "/SAFESEH:NO")
endif ()
if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif ()

project(
        NavKit
        VERSION 1.0.0
        DESCRIPTION "An app to create NAVP and AIRG files for use with Hitman"
        LANGUAGES CXX)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif ()
include("${PROJECT_SOURCE_DIR}/extern/vcpkg/scripts/buildsystems/vcpkg.cmake")
find_package(recastnavigation CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLUT REQUIRED)
find_package(GLU REQUIRED)
find_package(GLEW REQUIRED)
find_package(simdjson REQUIRED)
find_package(ftgl REQUIRED)
find_package(cpptrace CONFIG REQUIRED)

#find_package(nfd REQUIRED) # Reenable this once there is a new release of nfd
find_path(SIMPLEINI_INCLUDE_DIRS "ConvertUTF.c")

add_subdirectory(src)
add_subdirectory(tests)
