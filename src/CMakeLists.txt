file(GLOB INCLUDES include/*.h)
file(GLOB INCLUDES extern/*.h)
include_directories(SYSTEM ${OPENGL_INCLUDE_DIR})
include_directories(SYSTEM extern/fastlz)
include_directories(SYSTEM extern)
include_directories(SYSTEM include/RecastDemo)
include_directories(SYSTEM include)

configure_file("${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h.in" "${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h")

add_executable (NavKit "NavKit.cpp"  "${NavKit_SOURCE_DIR}/extern/fastlz/fastlz.c" "RecastDemo/imgui.cpp" "RecastDemo/imguiRenderGL.cpp")
target_include_directories(NavKit PUBLIC "${PROJECT_BINARY_DIR}")
target_link_libraries(NavKit PRIVATE ${OPENGL_LIBRARIES} ${GLUT_LIBRARY} )
target_link_libraries(NavKit PRIVATE OpenGL::GLU)
target_link_libraries(NavKit PRIVATE OpenGL::GL)
target_link_libraries(NavKit PRIVATE GLUT::GLUT)
target_link_libraries(NavKit PRIVATE RecastNavigation::Recast)
target_link_libraries(NavKit PRIVATE RecastNavigation::DebugUtils)
target_link_libraries(NavKit PRIVATE RecastNavigation::Detour)
target_link_libraries(NavKit PRIVATE RecastNavigation::DetourCrowd)
target_link_libraries(NavKit PRIVATE RecastNavigation::DetourTileCache)

file(COPY "${NavKit_SOURCE_DIR}/bin/DroidSans.ttf" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
#TARGET_LINK_LIBRARIES(NavKit PRIVATE NavWeakness::NavWeakness)

#target_link_libraries(NavKit PRIVATE "D:\\workspace\\NavKit\\External\\NavWeakness\\NavWeakness.lib")

target_link_libraries(NavKit
    PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET NavKit PROPERTY CXX_STANDARD 20)
endif()
