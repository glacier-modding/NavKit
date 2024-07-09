cmake_minimum_required (VERSION 3.28)

project(
  NavKit 
  VERSION 0.1
  DESCRIPTION "An app to create NAVP and AIRG files for use with Hitman"
  LANGUAGES CXX)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()
include("${PROJECT_SOURCE_DIR}/extern/vcpkg/scripts/buildsystems/vcpkg.cmake")
find_package(recastnavigation CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLUT REQUIRED)
find_package(GLU REQUIRED)
find_package(simdjson REQUIRED)

add_subdirectory(src)
#FIND_LIBRARY(NAVWEAKNESS_LIBRARIES NAMES navweakness NavWeakness.lib PATHS "External/NavWeakness/NavWeakness.lib")
