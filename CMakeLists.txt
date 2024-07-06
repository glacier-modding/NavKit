# CMakeList.txt : CMake project for NavKit, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("NavKit")

find_package(recastnavigation CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
# find_package(GLUT REQUIRED)
# include_directories( ${OPENGL_INCLUDE_DIRS}  ${GLUT_INCLUDE_DIRS} )

add_executable (NavKit "NavKit.cpp" "NavKit.h" "External/fastlz/fastlz.c" "External/fastlz/fastlz.h")

target_link_libraries(NavKit PRIVATE OpenGL::GL)
target_link_libraries(NavKit PRIVATE RecastNavigation::Recast)
target_link_libraries(NavKit PRIVATE RecastNavigation::DebugUtils)
target_link_libraries(NavKit PRIVATE RecastNavigation::Detour)
target_link_libraries(NavKit PRIVATE RecastNavigation::DetourCrowd)
target_link_libraries(NavKit PRIVATE RecastNavigation::DetourTileCache)
# target_link_libraries(NavKit ${OPENGL_LIBRARIES} ${GLUT_LIBRARY} )

target_link_libraries(NavKit
    PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET NavKit PROPERTY CXX_STANDARD 20)
endif()
