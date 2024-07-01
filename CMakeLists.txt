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

# Add source to this project's executable.
add_executable (NavKit "NavKit.cpp" "NavKit.h")

target_link_libraries(NavKit PRIVATE RecastNavigation::Recast)

target_link_libraries(NavKit
    PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET NavKit PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
